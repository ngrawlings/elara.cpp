#include "ElaraOsApp.h"
#include "ElaraOsEpaDebugService.h"
#include "ElaraOsEpaFrame.h"

#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarasockets/rpc/brpc/BRpcCodec.h>
#include <libelarasockets/rpc/brpc/BRpcRpcCodec.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;
using sockets::rpc::brpc::BRPC_ARRAY;
using sockets::rpc::brpc::BRPC_NAMED_BYTE;
using sockets::rpc::brpc::BRPC_NAMED_STRING;
using sockets::rpc::brpc::BRpcReader;
using sockets::rpc::brpc::BRpcRpcCodec;
using sockets::rpc::brpc::BRpcWriter;

ElaraOsApp::ElaraOsApp(
    const String &value_host,
    int value_port,
    const String &value_host_bridge_host,
    int value_host_bridge_port,
    const String &value_epa_dbg_host,
    int value_epa_dbg_port,
    const String &value_bundle_path,
    bool value_prefer_owned_ui_server
)
    : host(value_host),
      port(value_port),
      host_bridge_host(value_host_bridge_host),
      host_bridge_port(value_host_bridge_port),
      host_bridge_fd(-1),
      epa_dbg_host(value_epa_dbg_host),
      epa_dbg_port(value_epa_dbg_port),
      bundle_path(value_bundle_path),
      epa_dbg_fd(-1),
      epa_loaded(false),
      epa_dbg_last_error(""),
      boot_payload_pending(false),
      pending_boot_payload_hex(""),
      pending_boot_descriptor_params_json(""),
      virtual_drive_root(""),
      ext_logic_session_path(""),
      owned_ui_server_pid(-1),
      owned_python_pid(-1),
      prefer_owned_ui_server(value_prefer_owned_ui_server),
      host_bridge_running(false),
      quit_requested(false),
      ext_logic_server_fd(-1),
      peer(new ElaraUiRpcPeer()) {
    const char *home_env = getenv("HOME");
    virtual_drive_root = std::string(home_env && home_env[0] ? home_env : "/tmp") + "/.elaraos";
    const char *drive_root_env = getenv("ELARA_OS_VDRIVE_ROOT");
    if (drive_root_env && drive_root_env[0]) {
        virtual_drive_root = drive_root_env;
    }
    if (!bundle_path.length()) {
        bundle_path = String("../build/epa_firmware.bin");
    }
    const char *session_env = getenv("ELARA_OS_EXT_LOGIC_SESSION");
    ext_logic_session_path = session_env && session_env[0]
        ? std::string(session_env)
        : std::string("/tmp/elara-os-ext-logic-session.json");
}

ElaraOsApp::~ElaraOsApp() {
    stopHostDebugBridge();
    if (ext_logic_server_fd >= 0) {
        close(ext_logic_server_fd);
        ext_logic_server_fd = -1;
    }
    stopOwnedPythonLogic();
    stopOwnedUiServer();
    closeEpaDbg();
}

static std::string jsonEscape(const char *value) {
    std::string out;
    if (!value) {
        return out;
    }
    for (const char *p = value; *p; ++p) {
        switch (*p) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += *p; break;
        }
    }
    return out;
}

static std::string jsonStringField(const std::string &json, const char *key) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    std::string::size_type pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::string();
    }
    pos = json.find('"', pos + 1u);
    if (pos == std::string::npos) {
        return std::string();
    }
    std::string value;
    bool escaped = false;
    for (std::string::size_type i = pos + 1u; i < json.size(); ++i) {
        char ch = json[i];
        if (escaped) {
            value += ch;
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        value += ch;
    }
    return value;
}

static int jsonIntField(const std::string &json, const char *key, int fallback) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    std::string::size_type pos = json.find(needle);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }
    char *end = NULL;
    long value = strtol(json.c_str() + pos, &end, 10);
    return end && end != json.c_str() + pos ? (int)value : fallback;
}

static int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

static bool decodeHexBytes(const std::string &hex, std::vector<unsigned char> &bytes) {
    if ((hex.size() & 1u) != 0u) {
        return false;
    }
    bytes.clear();
    bytes.reserve(hex.size() / 2u);
    for (std::string::size_type i = 0; i < hex.size(); i += 2u) {
        int hi = hexNibble(hex[i]);
        int lo = hexNibble(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            bytes.clear();
            return false;
        }
        bytes.push_back((unsigned char)((hi << 4) | lo));
    }
    return true;
}

static void appendJsonCommand(String &json, int &emitted, const String &command_json) {
    if (emitted) {
        json += String(",");
    }
    json += command_json;
    emitted++;
}

static std::string readTextFile(const char *path) {
    if (!path || !path[0]) {
        return std::string();
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return std::string();
    }
    std::string text;
    char buffer[1024];
    size_t got = 0;
    while ((got = fread(buffer, 1u, sizeof(buffer), fp)) > 0u) {
        text.append(buffer, got);
    }
    fclose(fp);
    return text;
}

struct BootDriveInfo {
    int drive_id;
    std::string path;
    int flags;
    int block_size;
    int block_count;

    BootDriveInfo() : drive_id(0), path(), flags(0), block_size(0), block_count(0) {}
};

struct GptPartitionInfo {
    int drive_id;
    int partition_drive_id;
    int partition_index;
    uint64_t first_lba;
    uint64_t last_lba;
    std::string name;
    int fs_kind;
    int flags;

    GptPartitionInfo()
        : drive_id(0), partition_drive_id(0), partition_index(0), first_lba(0), last_lba(0), name(), fs_kind(1), flags(0) {}
};

static uint32_t hashU32Literal(const char *text) {
    uint32_t hash = 2166136261u;
    if (!text) {
        return hash;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        hash ^= (uint32_t)(*p);
        hash *= 16777619u;
    }
    return hash;
}

static int partitionDriveIdFor(int drive_id, int partition_index) {
    return (drive_id << 8) | (partition_index & 0xff);
}

static std::vector<BootDriveInfo> parseBootDriveInfos(const std::string &json) {
    std::vector<BootDriveInfo> drives;
    std::string::size_type pos = 0;
    while ((pos = json.find("\"drive_id\"", pos)) != std::string::npos) {
        std::string::size_type obj_start = json.rfind('{', pos);
        std::string::size_type obj_end = json.find('}', pos);
        if (obj_start == std::string::npos || obj_end == std::string::npos || obj_end <= obj_start) {
            break;
        }
        std::string object_json = json.substr(obj_start, obj_end - obj_start + 1u);
        BootDriveInfo drive;
        drive.drive_id = jsonIntField(object_json, "drive_id", 0);
        drive.path = jsonStringField(object_json, "path");
        drive.flags = jsonIntField(object_json, "flags", 0);
        drive.block_size = jsonIntField(object_json, "block_size", 0);
        drive.block_count = jsonIntField(object_json, "block_count", 0);
        if (drive.drive_id > 0 && !drive.path.empty()) {
            drives.push_back(drive);
        }
        pos = obj_end + 1u;
    }
    return drives;
}

static uint32_t readLeU32At(const unsigned char *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t readLeU16At(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static uint64_t readLeU64At(const unsigned char *p) {
    return ((uint64_t)p[0])
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

static std::string readUtf16LeName(const unsigned char *bytes, size_t max_bytes) {
    std::string out;
    for (size_t i = 0; i + 1u < max_bytes; i += 2u) {
        uint16_t code_unit = (uint16_t)bytes[i] | (uint16_t)(bytes[i + 1u] << 8);
        if (code_unit == 0u) {
            break;
        }
        if (code_unit < 0x80u) {
            out.push_back((char)code_unit);
        }
    }
    return out;
}

static bool scanGptPartitions(const BootDriveInfo &drive, std::vector<GptPartitionInfo> &partitions, std::string &error_message) {
    partitions.clear();
    FILE *fp = fopen(drive.path.c_str(), "rb");
    if (!fp) {
        error_message = std::string("failed to open drive image: ") + drive.path;
        return false;
    }

    unsigned char header[512];
    memset(header, 0, sizeof(header));
    if (fseeko(fp, 512, SEEK_SET) != 0 || fread(header, 1u, sizeof(header), fp) < 92u) {
        fclose(fp);
        error_message = std::string("failed to read GPT header from: ") + drive.path;
        return false;
    }
    if (memcmp(header, "EFI PART", 8) != 0) {
        fclose(fp);
        error_message = std::string("missing GPT signature in: ") + drive.path;
        return false;
    }

    uint64_t entry_lba = readLeU64At(header + 72u);
    uint32_t entry_count = readLeU32At(header + 80u);
    uint32_t entry_size = readLeU32At(header + 84u);
    if (entry_count == 0u || entry_size < 128u) {
        fclose(fp);
        error_message = std::string("invalid GPT entry table in: ") + drive.path;
        return false;
    }

    if (entry_count > 128u) {
        entry_count = 128u;
    }
    std::vector<unsigned char> table((size_t)entry_count * (size_t)entry_size, 0);
    off_t table_offset = (off_t)(entry_lba * 512u);
    if (fseeko(fp, table_offset, SEEK_SET) != 0 ||
        fread(table.data(), 1u, table.size(), fp) < table.size()) {
        fclose(fp);
        error_message = std::string("failed to read GPT entries from: ") + drive.path;
        return false;
    }
    fclose(fp);

    for (uint32_t i = 0; i < entry_count; ++i) {
        const unsigned char *entry = table.data() + ((size_t)i * (size_t)entry_size);
        bool empty = true;
        for (size_t b = 0; b < 16u; ++b) {
            if (entry[b] != 0u) {
                empty = false;
                break;
            }
        }
        if (empty) {
            continue;
        }

        GptPartitionInfo info;
        info.drive_id = drive.drive_id;
        info.partition_index = (int)(i + 1u);
        info.partition_drive_id = partitionDriveIdFor(info.drive_id, info.partition_index);
        info.first_lba = readLeU64At(entry + 32u);
        info.last_lba = readLeU64At(entry + 40u);
        info.name = readUtf16LeName(entry + 56u, 72u);
        info.fs_kind = 1;
        info.flags = 0;
        if (info.name == "rootfs" || info.name == "root") {
            info.flags = 1;
        }
        partitions.push_back(info);
    }

    if (drive.drive_id == 1) {
        bool have_root = false;
        for (size_t i = 0; i < partitions.size(); ++i) {
            if (partitions[i].flags == 1) {
                have_root = true;
                break;
            }
        }
        if (!have_root && !partitions.empty()) {
            partitions[0].flags = 1;
        }
    }

    error_message.clear();
    return true;
}

static void appendLeU32Hex(std::string &hex, uint32_t value) {
    char chunk[9];
    snprintf(chunk, sizeof(chunk), "%02x%02x%02x%02x",
             (unsigned)(value & 0xffu),
             (unsigned)((value >> 8) & 0xffu),
             (unsigned)((value >> 16) & 0xffu),
             (unsigned)((value >> 24) & 0xffu));
    hex += chunk;
}

static void appendBytesHex(std::string &hex, const std::vector<unsigned char> &bytes) {
    char chunk[3];
    for (size_t i = 0; i < bytes.size(); ++i) {
        snprintf(chunk, sizeof(chunk), "%02x", (unsigned)bytes[i]);
        hex += chunk;
    }
}

static String ext4SuperblockSummaryJson(
    const GptPartitionInfo &partition,
    const std::vector<unsigned char> &superblock
) {
    if (superblock.size() < 136u) {
        return String("{\"valid\":false,\"error\":\"short superblock\"}");
    }
    uint32_t log_block_size = readLeU32At(superblock.data() + 24u);
    uint32_t block_size = 1024u;
    for (uint32_t i = 0; i < log_block_size && i < 16u; ++i) {
        block_size *= 2u;
    }
    char volume_name[17];
    memset(volume_name, 0, sizeof(volume_name));
    memcpy(volume_name, superblock.data() + 120u, 16u);
    return String("{\"valid\":") + String(readLeU16At(superblock.data() + 56u) == 0xef53u ? "true" : "false")
        + String(",\"partition_drive_id\":") + String(partition.partition_drive_id)
        + String(",\"magic\":") + String((int)readLeU16At(superblock.data() + 56u))
        + String(",\"block_size\":") + String((int)block_size)
        + String(",\"block_count\":") + String((int)readLeU32At(superblock.data() + 4u))
        + String(",\"inode_count\":") + String((int)readLeU32At(superblock.data() + 0u))
        + String(",\"blocks_per_group\":") + String((int)readLeU32At(superblock.data() + 32u))
        + String(",\"inodes_per_group\":") + String((int)readLeU32At(superblock.data() + 40u))
        + String(",\"inode_size\":") + String((int)readLeU16At(superblock.data() + 88u))
        + String(",\"feature_compat\":") + String((int)readLeU32At(superblock.data() + 92u))
        + String(",\"feature_incompat\":") + String((int)readLeU32At(superblock.data() + 96u))
        + String(",\"feature_ro_compat\":") + String((int)readLeU32At(superblock.data() + 100u))
        + String(",\"volume_name\":\"") + String(jsonEscape(volume_name).c_str()) + String("\"")
        + String("}");
}

static bool readPartitionBytes(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint64_t partition_relative_offset,
    size_t byte_count,
    std::vector<unsigned char> &bytes,
    std::string &error_message
) {
    bytes.assign(byte_count, 0);
    FILE *fp = fopen(drive.path.c_str(), "rb");
    if (!fp) {
        error_message = std::string("failed to open drive image for partition read: ") + drive.path;
        return false;
    }

    uint64_t absolute_offset = (partition.first_lba * 512ull) + partition_relative_offset;
    if (fseeko(fp, (off_t)absolute_offset, SEEK_SET) != 0 ||
        fread(bytes.data(), 1u, byte_count, fp) < byte_count) {
        fclose(fp);
        error_message = std::string("failed to read partition bytes from: ") + drive.path;
        return false;
    }

    fclose(fp);
    error_message.clear();
    return true;
}

struct Ext4RootInodeSummary {
    uint32_t inode_table_block;
    uint32_t mode;
    uint32_t size_lo;
    uint32_t blocks_lo;
    uint32_t flags;
    uint32_t extent_magic;
    uint32_t extent_entries;
    uint32_t extent_depth;
    uint32_t extent_len;
    uint32_t extent_start_block_lo;
    bool valid;

    Ext4RootInodeSummary()
        : inode_table_block(0),
          mode(0),
          size_lo(0),
          blocks_lo(0),
          flags(0),
          extent_magic(0),
          extent_entries(0),
          extent_depth(0),
          extent_len(0),
          extent_start_block_lo(0),
          valid(false) {}
};

struct Ext4DirectoryEntrySummary {
    uint32_t inode_number;
    uint32_t name_hash;
    uint32_t file_type;
    uint32_t name_len;
    uint32_t rec_len;
    std::string name;

    Ext4DirectoryEntrySummary()
        : inode_number(0), name_hash(0), file_type(0), name_len(0), rec_len(0), name() {}
};

struct Ext4ExtentSummary {
    uint32_t logical_block;
    uint32_t len;
    uint64_t start_block;

    Ext4ExtentSummary() : logical_block(0), len(0), start_block(0) {}
};

struct Ext4InodeSummary {
    uint32_t inode_number;
    uint32_t inode_table_block;
    uint32_t mode;
    uint32_t size_lo;
    uint32_t blocks_lo;
    uint32_t flags;
    uint32_t extent_magic;
    uint32_t extent_entries;
    uint32_t extent_depth;
    std::vector<Ext4ExtentSummary> extents;
    bool valid;

    Ext4InodeSummary()
        : inode_number(0),
          inode_table_block(0),
          mode(0),
          size_lo(0),
          blocks_lo(0),
          flags(0),
          extent_magic(0),
          extent_entries(0),
          extent_depth(0),
          extents(),
          valid(false) {}
};

static bool readExt4InodeSummary(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    uint32_t inode_size,
    uint32_t inodes_per_group,
    uint32_t inode_number,
    Ext4InodeSummary &summary,
    std::string &error_message
) {
    if (block_size == 0u || inode_size < 128u || inodes_per_group == 0u || inode_number == 0u) {
        error_message = "invalid ext4 geometry for inode read";
        return false;
    }

    uint32_t inode_index = inode_number - 1u;
    uint32_t group = inode_index / inodes_per_group;
    uint32_t index_in_group = inode_index % inodes_per_group;
    uint64_t descriptor_offset = ((block_size == 1024u) ? 2048ull : (uint64_t)block_size) + ((uint64_t)group * 64ull);
    std::vector<unsigned char> group_descriptor;
    if (!readPartitionBytes(drive, partition, descriptor_offset, 64u, group_descriptor, error_message)) {
        return false;
    }

    summary = Ext4InodeSummary();
    summary.inode_number = inode_number;
    summary.inode_table_block = readLeU32At(group_descriptor.data() + 8u);
    if (summary.inode_table_block == 0u) {
        error_message = "ext4 group descriptor has no inode table block";
        return false;
    }

    uint64_t inode_offset = ((uint64_t)summary.inode_table_block * (uint64_t)block_size)
        + ((uint64_t)index_in_group * (uint64_t)inode_size);
    std::vector<unsigned char> inode;
    if (!readPartitionBytes(drive, partition, inode_offset, inode_size, inode, error_message)) {
        return false;
    }

    summary.mode = (uint32_t)readLeU16At(inode.data() + 0u);
    summary.size_lo = readLeU32At(inode.data() + 4u);
    summary.blocks_lo = readLeU32At(inode.data() + 28u);
    summary.flags = readLeU32At(inode.data() + 32u);
    summary.extent_magic = (uint32_t)readLeU16At(inode.data() + 40u);
    summary.extent_entries = (uint32_t)readLeU16At(inode.data() + 42u);
    summary.extent_depth = (uint32_t)readLeU16At(inode.data() + 46u);
    if (summary.extent_magic != 0xf30au || summary.extent_depth != 0u) {
        error_message = "ext4 inode is not a directly readable extent leaf";
        return false;
    }

    uint32_t entries = summary.extent_entries;
    if (entries > 4u) {
        entries = 4u;
    }
    for (uint32_t i = 0; i < entries; ++i) {
        size_t off = 52u + ((size_t)i * 12u);
        if (off + 12u > inode.size()) {
            break;
        }
        Ext4ExtentSummary extent;
        extent.logical_block = readLeU32At(inode.data() + off);
        extent.len = (uint32_t)readLeU16At(inode.data() + off + 4u);
        uint32_t start_hi = (uint32_t)readLeU16At(inode.data() + off + 6u);
        uint32_t start_lo = readLeU32At(inode.data() + off + 8u);
        extent.start_block = ((uint64_t)start_hi << 32) | (uint64_t)start_lo;
        if (extent.len > 0u) {
            summary.extents.push_back(extent);
        }
    }

    summary.valid = !summary.extents.empty();
    error_message.clear();
    return true;
}

static bool readExt4RootInodeSummary(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    uint32_t inode_size,
    Ext4RootInodeSummary &summary,
    std::string &error_message
) {
    if (block_size == 0u || inode_size < 128u) {
        error_message = "invalid ext4 geometry for root inode read";
        return false;
    }

    std::vector<unsigned char> group_descriptor;
    uint64_t group_descriptor_offset = (block_size == 1024u) ? 2048ull : (uint64_t)block_size;
    if (!readPartitionBytes(drive, partition, group_descriptor_offset, 64u, group_descriptor, error_message)) {
        return false;
    }

    summary.inode_table_block = readLeU32At(group_descriptor.data() + 8u);
    if (summary.inode_table_block == 0u) {
        error_message = "ext4 group descriptor has no inode table block";
        return false;
    }

    uint64_t root_inode_index = 1ull;
    uint64_t root_inode_offset = ((uint64_t)summary.inode_table_block * (uint64_t)block_size)
        + (root_inode_index * (uint64_t)inode_size);
    std::vector<unsigned char> root_inode;
    if (!readPartitionBytes(drive, partition, root_inode_offset, inode_size, root_inode, error_message)) {
        return false;
    }

    summary.mode = (uint32_t)readLeU16At(root_inode.data() + 0u);
    summary.size_lo = readLeU32At(root_inode.data() + 4u);
    summary.blocks_lo = readLeU32At(root_inode.data() + 28u);
    summary.flags = readLeU32At(root_inode.data() + 32u);
    summary.extent_magic = (uint32_t)readLeU16At(root_inode.data() + 40u);
    summary.extent_entries = (uint32_t)readLeU16At(root_inode.data() + 42u);
    summary.extent_depth = (uint32_t)readLeU16At(root_inode.data() + 46u);
    if (summary.extent_depth == 0u && summary.extent_entries > 0u && root_inode.size() >= 64u) {
        summary.extent_len = (uint32_t)readLeU16At(root_inode.data() + 56u);
        summary.extent_start_block_lo = readLeU32At(root_inode.data() + 60u);
    }
    summary.valid = ((summary.mode & 0x4000u) == 0x4000u) && summary.extent_magic == 0xf30au;
    error_message.clear();
    return true;
}

static String ext4RootInodeSummaryJson(const Ext4RootInodeSummary &summary) {
    return String("{\"valid\":") + String(summary.valid ? "true" : "false")
        + String(",\"inode_number\":2")
        + String(",\"inode_table_block\":") + String((int)summary.inode_table_block)
        + String(",\"mode\":") + String((int)summary.mode)
        + String(",\"directory\":") + String((summary.mode & 0x4000u) == 0x4000u ? "true" : "false")
        + String(",\"size_lo\":") + String((int)summary.size_lo)
        + String(",\"blocks_lo\":") + String((int)summary.blocks_lo)
        + String(",\"flags\":") + String((int)summary.flags)
        + String(",\"extent_magic\":") + String((int)summary.extent_magic)
        + String(",\"extent_entries\":") + String((int)summary.extent_entries)
        + String(",\"extent_depth\":") + String((int)summary.extent_depth)
        + String(",\"extent_len\":") + String((int)summary.extent_len)
        + String(",\"extent_start_block_lo\":") + String((int)summary.extent_start_block_lo)
        + String("}");
}

static bool readExt4RootDirectoryEntries(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    const Ext4RootInodeSummary &root_inode,
    std::vector<Ext4DirectoryEntrySummary> &entries,
    std::string &error_message
) {
    entries.clear();
    if (!root_inode.valid || root_inode.extent_depth != 0u || root_inode.extent_start_block_lo == 0u) {
        error_message = "root inode does not have a directly readable extent leaf";
        return false;
    }

    std::vector<unsigned char> block;
    uint64_t offset = (uint64_t)root_inode.extent_start_block_lo * (uint64_t)block_size;
    if (!readPartitionBytes(drive, partition, offset, block_size, block, error_message)) {
        return false;
    }

    size_t pos = 0u;
    while (pos + 8u <= block.size() && entries.size() < 64u) {
        uint32_t inode_number = readLeU32At(block.data() + pos);
        uint32_t rec_len = (uint32_t)readLeU16At(block.data() + pos + 4u);
        uint32_t name_len = (uint32_t)block[pos + 6u];
        uint32_t file_type = (uint32_t)block[pos + 7u];
        if (rec_len < 8u || pos + rec_len > block.size()) {
            break;
        }
        if (inode_number > 0u && name_len > 0u && name_len <= rec_len - 8u) {
            Ext4DirectoryEntrySummary entry;
            entry.inode_number = inode_number;
            entry.file_type = file_type;
            entry.name_len = name_len;
            entry.rec_len = rec_len;
            entry.name.assign((const char *)(block.data() + pos + 8u), name_len);
            entry.name_hash = hashU32Literal(entry.name.c_str());
            entries.push_back(entry);
        }
        pos += rec_len;
    }

    error_message.clear();
    return true;
}

static bool readExt4DirectoryEntriesForInode(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    const Ext4InodeSummary &inode,
    std::vector<Ext4DirectoryEntrySummary> &entries,
    std::string &error_message
) {
    entries.clear();
    if (!inode.valid || (inode.mode & 0x4000u) != 0x4000u) {
        error_message = "ext4 inode is not a readable directory";
        return false;
    }

    uint64_t remaining = inode.size_lo ? (uint64_t)inode.size_lo : (uint64_t)block_size;
    for (size_t ei = 0; ei < inode.extents.size() && remaining > 0u; ++ei) {
        const Ext4ExtentSummary &extent = inode.extents[ei];
        for (uint32_t bi = 0; bi < extent.len && remaining > 0u && entries.size() < 128u; ++bi) {
            std::vector<unsigned char> block;
            uint64_t offset = (extent.start_block + (uint64_t)bi) * (uint64_t)block_size;
            if (!readPartitionBytes(drive, partition, offset, block_size, block, error_message)) {
                return false;
            }
            size_t limit = block.size();
            if (remaining < limit) {
                limit = (size_t)remaining;
            }
            size_t pos = 0u;
            while (pos + 8u <= limit && entries.size() < 128u) {
                uint32_t inode_number = readLeU32At(block.data() + pos);
                uint32_t rec_len = (uint32_t)readLeU16At(block.data() + pos + 4u);
                uint32_t name_len = (uint32_t)block[pos + 6u];
                uint32_t file_type = (uint32_t)block[pos + 7u];
                if (rec_len < 8u || pos + rec_len > limit) {
                    break;
                }
                if (inode_number > 0u && name_len > 0u && name_len <= rec_len - 8u) {
                    Ext4DirectoryEntrySummary entry;
                    entry.inode_number = inode_number;
                    entry.file_type = file_type;
                    entry.name_len = name_len;
                    entry.rec_len = rec_len;
                    entry.name.assign((const char *)(block.data() + pos + 8u), name_len);
                    entry.name_hash = hashU32Literal(entry.name.c_str());
                    entries.push_back(entry);
                }
                pos += rec_len;
            }
            remaining -= limit;
        }
    }

    error_message.clear();
    return true;
}

static bool readExt4FileBytesForInode(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    const Ext4InodeSummary &inode,
    std::vector<unsigned char> &bytes,
    std::string &error_message
) {
    bytes.clear();
    if (!inode.valid || (inode.mode & 0x8000u) != 0x8000u) {
        error_message = "ext4 inode is not a readable regular file";
        return false;
    }
    bytes.reserve(inode.size_lo);
    uint64_t remaining = inode.size_lo;
    for (size_t ei = 0; ei < inode.extents.size() && remaining > 0u; ++ei) {
        const Ext4ExtentSummary &extent = inode.extents[ei];
        for (uint32_t bi = 0; bi < extent.len && remaining > 0u; ++bi) {
            std::vector<unsigned char> block;
            uint64_t offset = (extent.start_block + (uint64_t)bi) * (uint64_t)block_size;
            if (!readPartitionBytes(drive, partition, offset, block_size, block, error_message)) {
                return false;
            }
            size_t take = block.size();
            if (remaining < take) {
                take = (size_t)remaining;
            }
            bytes.insert(bytes.end(), block.begin(), block.begin() + (std::vector<unsigned char>::difference_type)take);
            remaining -= take;
        }
    }
    if (bytes.size() != inode.size_lo) {
        error_message = "ext4 file extents ended before file size";
        return false;
    }
    error_message.clear();
    return true;
}

static bool findExt4DirectoryEntry(
    const std::vector<Ext4DirectoryEntrySummary> &entries,
    const char *name,
    Ext4DirectoryEntrySummary &out_entry
) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == name) {
            out_entry = entries[i];
            return true;
        }
    }
    return false;
}

static bool readExt4KernelImage(
    const BootDriveInfo &drive,
    const GptPartitionInfo &partition,
    uint32_t block_size,
    uint32_t inode_size,
    uint32_t inodes_per_group,
    std::vector<unsigned char> &kernel_image,
    std::string &error_message
) {
    Ext4InodeSummary root;
    Ext4InodeSummary boot_dir;
    Ext4InodeSummary elara_dir;
    Ext4InodeSummary kernel_file;
    std::vector<Ext4DirectoryEntrySummary> entries;
    Ext4DirectoryEntrySummary boot_entry;
    Ext4DirectoryEntrySummary elara_entry;
    Ext4DirectoryEntrySummary kernel_entry;

    if (!readExt4InodeSummary(drive, partition, block_size, inode_size, inodes_per_group, 2u, root, error_message) ||
        !readExt4DirectoryEntriesForInode(drive, partition, block_size, root, entries, error_message)) {
        return false;
    }
    if (!findExt4DirectoryEntry(entries, "boot", boot_entry)) {
        error_message = "missing /boot directory in virtual root";
        return false;
    }
    if (!readExt4InodeSummary(drive, partition, block_size, inode_size, inodes_per_group, boot_entry.inode_number, boot_dir, error_message) ||
        !readExt4DirectoryEntriesForInode(drive, partition, block_size, boot_dir, entries, error_message)) {
        return false;
    }
    if (!findExt4DirectoryEntry(entries, "elara", elara_entry)) {
        error_message = "missing /boot/elara directory in virtual root";
        return false;
    }
    if (!readExt4InodeSummary(drive, partition, block_size, inode_size, inodes_per_group, elara_entry.inode_number, elara_dir, error_message) ||
        !readExt4DirectoryEntriesForInode(drive, partition, block_size, elara_dir, entries, error_message)) {
        return false;
    }
    if (!findExt4DirectoryEntry(entries, "epa_kernel.bin", kernel_entry)) {
        error_message = "missing /boot/elara/epa_kernel.bin in virtual root";
        return false;
    }
    if (!readExt4InodeSummary(drive, partition, block_size, inode_size, inodes_per_group, kernel_entry.inode_number, kernel_file, error_message) ||
        !readExt4FileBytesForInode(drive, partition, block_size, kernel_file, kernel_image, error_message)) {
        return false;
    }
    error_message.clear();
    return true;
}

static String ext4DirectoryEntriesSummaryJson(const std::vector<Ext4DirectoryEntrySummary> &entries) {
    String json("[");
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0u) {
            json = json + String(",");
        }
        json = json + String("{\"inode_number\":") + String((int)entries[i].inode_number)
            + String(",\"name\":\"") + String(jsonEscape(entries[i].name.c_str()).c_str()) + String("\"")
            + String(",\"name_hash\":") + String((int)entries[i].name_hash)
            + String(",\"file_type\":") + String((int)entries[i].file_type)
            + String(",\"name_len\":") + String((int)entries[i].name_len)
            + String(",\"rec_len\":") + String((int)entries[i].rec_len)
            + String("}");
    }
    json = json + String("]");
    return json;
}

static String jsonQuoteString(const String &value) {
    String value_copy(value);
    std::string escaped = jsonEscape(value_copy.operator char *());
    return String("\"") + String(escaped.c_str()) + String("\"");
}

static bool readExact(int fd, char *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t got = read(fd, buffer + offset, length - offset);
        if (got <= 0) {
            return false;
        }
        offset += (size_t)got;
    }
    return true;
}

static bool readBrpcFrame(int fd, std::vector<char> &frame) {
    unsigned char hdr[4];
    if (!readExact(fd, (char *)hdr, 4)) {
        return false;
    }
    uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
                 | ((uint32_t)hdr[2] << 8)  | (uint32_t)hdr[3];
    if (len > 4u * 1024u * 1024u) {
        return false;
    }
    frame.resize(len);
    return readExact(fd, frame.data(), len);
}

static bool writeBrpcFrame(int fd, const ByteArray &payload) {
    ByteArray framed = BRpcRpcCodec::framePayload(payload);
    return write(fd, framed.operator const char *(), (size_t)framed.length()) == (ssize_t)framed.length();
}

static void dispatchExtLogicFrame(ElaraOsApp *app, int fd, const std::vector<char> &frame) {
    String id;
    String method;
    String params_json;
    String parse_error;
    if (!BRpcRpcCodec::parseRequest(frame.data(), frame.size(), id, method, params_json, parse_error) || !id.length()) {
        return;
    }

    if (method == String("ext.ping")) {
        writeBrpcFrame(fd, BRpcRpcCodec::buildSuccessResponse(id, String("\"pong\"")));
        return;
    }
    if (method == String("ext.register")) {
        writeBrpcFrame(fd, BRpcRpcCodec::buildSuccessResponse(id, String("{\"ok\":true}")));
        return;
    }

    String result_json;
    String error_code;
    String error_message;
    bool handled = app && app->handleExtLogicRequest(method, params_json, result_json, error_code, error_message);
    if (handled) {
        writeBrpcFrame(fd, BRpcRpcCodec::buildSuccessResponse(id, result_json.length() ? result_json : String("{}")));
    } else {
        writeBrpcFrame(
            fd,
            BRpcRpcCodec::buildErrorResponse(
                id,
                error_code.length() ? error_code : String("not_found"),
                error_message.length() ? error_message : String("method not implemented on C++ host")
            )
        );
    }
}

void ElaraOsApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("Elara OS"), 1280, 720, String("org.elara.ui.elara-os"));
    ui.setThemeMode(String("dark"));
    ui.setRootContent(String("app.surface"));
    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("backend"), String("vulkan"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("elara.os.frame_io"));
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("Boot Pending"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setSectionJson(
        String("app.surface"),
        String("commands"),
        String("["
               "{\"op\":\"clear\",\"r\":0.039,\"g\":0.055,\"b\":0.078},"
               "{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":720,\"r\":0.039,\"g\":0.055,\"b\":0.078},"
               "{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":720,\"r\":0.055,\"g\":0.071,\"b\":0.094},"
               "{\"op\":\"text\",\"x\":460,\"y\":338,\"text\":\"Boot Pending\",\"size\":42,\"r\":0.92,\"g\":0.96,\"b\":1.0},"
               "{\"op\":\"text\",\"x\":462,\"y\":382,\"text\":\"waiting for EPA frame IO authority\",\"size\":18,\"r\":0.68,\"g\":0.78,\"b\":0.9}"
               "]")
    );
}

bool ElaraOsApp::loadDocument(const String &document_json) {
    String params = String("{\"document\":") + JsonString(document_json, true).toString() + String("}");
    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(String("ui.loadDocument"), params, result_json, error_code, error_message, 5000)) {
        printf("ui.loadDocument failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        return false;
    }
    printf("Document loaded: %s\n", result_json.operator char *());
    return true;
}

bool ElaraOsApp::setSectionJson(const String &target, const String &section, const String &value_json) {
    String params = String("{\"target\":")
        + JsonString(target, true).toString()
        + String(",\"section\":")
        + JsonString(section, true).toString()
        + String(",\"value\":")
        + value_json
        + String("}");
    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(String("ui.setSectionJson"), params, result_json, error_code, error_message, 5000)) {
        printf("ui.setSectionJson failed [%s] target=%s section=%s: %s\n",
               error_code.operator char *(),
               String(target).operator char *(),
               String(section).operator char *(),
               error_message.operator char *());
        return false;
    }
    printf("ui.setSectionJson ok: target=%s section=%s bytes=%d\n",
           String(target).operator char *(),
           String(section).operator char *(),
           value_json.length());
    return true;
}

bool ElaraOsApp::updateSurfaceCommandsFromMailbox(const String &mailbox_json, String &frame_json, String &error_message) {
    String mailbox_copy(mailbox_json);
    std::string mb(mailbox_copy.operator char *() ? mailbox_copy.operator char *() : "");
    std::string hex = jsonStringField(mb, "hex");
    int wid = jsonIntField(mb, "wid", 0);
    int len = jsonIntField(mb, "len", 0);
    if (wid <= 0 || len <= 0 || hex.empty()) {
        error_message = String("EPA mailbox did not contain an egress frame");
        return false;
    }

    std::vector<unsigned char> bytes;
    if (!decodeHexBytes(hex, bytes) || (int)bytes.size() != len) {
        error_message = String("EPA mailbox hex payload was malformed");
        return false;
    }

    ElaraOsEpaFrameHeader header = orangeFortressParseEgressFrameHeader(bytes.data(), bytes.size());
    frame_json = orangeFortressFrameHeaderJson(
        header,
        String("egress"),
        String("elara-os.epa.frame.v1")
    );
    sendHostDebugEvent(
        "egress_frame",
        (String("\"kernel\":\"elara.os.frame_io\",")
            + String("\"worker\":\"wid=") + String(wid) + String("\",")
            + String("\"frame\":") + frame_json).operator char *()
    );
    if (!header.valid) {
        error_message = String("invalid EPA egress frame: ") + header.error;
        return false;
    }

    size_t offset = header.header_bytes;
    String commands("[");
    int emitted = 0;
    int texture_width = 0;
    int texture_height = 0;
    int texture_pixels_expected = 0;
    int texture_pixels_seen = 0;
    String texture_rgb("[");
    appendJsonCommand(
        commands,
        emitted,
        String("{\"op\":\"clear\",\"r\":") + String(((double)header.frame_type) / 255.0)
            + String(",\"g\":") + String(((double)header.frame_id) / 255.0)
            + String(",\"b\":") + String(((double)header.record_count) / 255.0)
            + String("}")
    );

    while (offset + 4u <= bytes.size()) {
        uint32_t opcode = orangeFortressReadLeU32(bytes.data() + offset);
        offset += 4u;
        if (opcode == 255u) {
            break;
        }
        if (opcode == 1u) {
            if (offset + 28u > bytes.size()) break;
            uint32_t x = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t y = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t w = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t h = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t r = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t g = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t b = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            appendJsonCommand(
                commands,
                emitted,
                String("{\"op\":\"rect\",\"x\":") + String((int)x)
                    + String(",\"y\":") + String((int)y)
                    + String(",\"w\":") + String((int)w)
                    + String(",\"h\":") + String((int)h)
                    + String(",\"r\":") + String(((double)r) / 255.0)
                    + String(",\"g\":") + String(((double)g) / 255.0)
                    + String(",\"b\":") + String(((double)b) / 255.0)
                    + String("}")
            );
            continue;
        }
        if (opcode == 2u) {
            if (offset + 32u > bytes.size()) break;
            uint32_t x0 = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t y0 = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t x1 = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t y1 = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t line_width = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t r = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t g = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t b = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            appendJsonCommand(
                commands,
                emitted,
                String("{\"op\":\"line\",\"x0\":") + String((int)x0)
                    + String(",\"y0\":") + String((int)y0)
                    + String(",\"x1\":") + String((int)x1)
                    + String(",\"y1\":") + String((int)y1)
                    + String(",\"line_width\":") + String((int)line_width)
                    + String(",\"r\":") + String(((double)r) / 255.0)
                    + String(",\"g\":") + String(((double)g) / 255.0)
                    + String(",\"b\":") + String(((double)b) / 255.0)
                    + String("}")
            );
            continue;
        }
        if (opcode == 3u) {
            if (offset + 8u > bytes.size()) break;
            texture_width = (int)orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            texture_height = (int)orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            if (texture_width > 0 && texture_height > 0 && texture_width <= 256 && texture_height <= 256) {
                texture_pixels_expected = texture_width * texture_height;
                texture_pixels_seen = 0;
                texture_rgb = String("[");
            } else {
                texture_pixels_expected = 0;
                texture_pixels_seen = 0;
            }
            continue;
        }
        if (opcode == 4u) {
            if (offset + 4u > bytes.size()) break;
            uint32_t rgb = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            if (texture_pixels_expected > 0 && texture_pixels_seen < texture_pixels_expected) {
                if (texture_pixels_seen > 0) texture_rgb += String(",");
                texture_rgb += String((int)((rgb >> 16) & 255u));
                texture_rgb += String(",");
                texture_rgb += String((int)((rgb >> 8) & 255u));
                texture_rgb += String(",");
                texture_rgb += String((int)(rgb & 255u));
                texture_pixels_seen++;
            }
            continue;
        }
        if (opcode == 5u) {
            if (offset + 16u > bytes.size()) break;
            uint32_t x = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t y = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t w = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            uint32_t h = orangeFortressReadLeU32(bytes.data() + offset); offset += 4u;
            appendJsonCommand(
                commands,
                emitted,
                String("{\"op\":\"textured_rect\",\"x\":") + String((int)x)
                    + String(",\"y\":") + String((int)y)
                    + String(",\"w\":") + String((int)w)
                    + String(",\"h\":") + String((int)h)
                    + String("}")
            );
            continue;
        }
        break;
    }
    commands += String("]");
    texture_rgb += String("]");

    if (texture_pixels_expected > 0 && texture_pixels_seen == texture_pixels_expected) {
        String texture_json = String("{\"width\":") + String(texture_width)
            + String(",\"height\":") + String(texture_height)
            + String(",\"rgb\":") + texture_rgb
            + String("}");
        if (!setSectionJson(String("app.surface"), String("texture"), texture_json)) {
            error_message = String("failed to update Vulkan surface texture");
            return false;
        }
    }

    if (!setSectionJson(String("app.surface"), String("commands"), commands)) {
        error_message = String("failed to update Vulkan surface commands");
        return false;
    }
    sendHostDebugEvent("state", "\"status\":\"Elara OS boot screen egressed\",\"surface\":\"app.surface\"");
    printf("[C++ Host] Elara OS boot frame parsed: wid=%d bytes=%d records=%d commands=%d\n",
           wid, len, (int)header.record_count, emitted);
    return true;
}

bool ElaraOsApp::printSnapshot() {
    String result_json;
    String error_code;
    String error_message;
    if (peer->call(String("ui.snapshot"), String("{}"), result_json, error_code, error_message, 5000)) {
        printf("%s\n", result_json.operator char *());
        return true;
    }
    printf("ui.snapshot failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
    return false;
}

int ElaraOsApp::chooseUiFallbackPort() const {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) != 0) {
        close(fd);
        return 0;
    }
    int chosen_port = (int)ntohs(addr.sin_port);
    close(fd);
    return chosen_port;
}

void ElaraOsApp::recordLaunchedPid(const char *label, pid_t pid) const {
    const char *pid_file = getenv("ELARA_PID_FILE");
    if (!pid_file || !pid_file[0] || pid <= 0) {
        return;
    }
    FILE *out = fopen(pid_file, "a");
    if (!out) {
        return;
    }
    fprintf(out, "%d\t%s\n", (int)pid, label ? label : "elara-os-ui-head");
    fclose(out);
}

void ElaraOsApp::stopOwnedUiServer() {
    if (owned_ui_server_pid <= 0) {
        return;
    }
    pid_t pid = owned_ui_server_pid;
    owned_ui_server_pid = -1;
    kill(pid, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            return;
        }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void ElaraOsApp::stopOwnedPythonLogic() {
    if (owned_python_pid <= 0) {
        return;
    }
    pid_t pid = owned_python_pid;
    owned_python_pid = -1;
    kill(pid, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            return;
        }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

bool ElaraOsApp::launchPythonLogic() {
    if (owned_python_pid > 0) {
        return true;
    }

    const char *override_path = getenv("ELARA_OS_PYTHON_APP");
    std::string app_path = override_path && override_path[0]
        ? std::string(override_path)
        : std::string("../python/app.py");

    pid_t pid = fork();
    if (pid < 0) {
        printf("Failed to fork elara-os Python launcher: %s\n", strerror(errno));
        return false;
    }
    if (pid == 0) {
        if (!ext_logic_session_path.empty()) {
            setenv("ELARA_DEBUG_SESSION", ext_logic_session_path.c_str(), 1);
        }
        execlp("python3", "python3", app_path.c_str(), (char *)NULL);
        fprintf(stderr, "Failed to exec python3 %s: %s\n", app_path.c_str(), strerror(errno));
        _exit(127);
    }

    owned_python_pid = pid;
    recordLaunchedPid("elara-os-python", pid);
    printf("Spawned elara-os Python logic pid=%d app=%s\n", (int)pid, app_path.c_str());
    return true;
}

bool ElaraOsApp::launchUiServerFallback() {
    if (owned_ui_server_pid > 0) {
        return true;
    }
    int fallback_port = chooseUiFallbackPort();
    if (fallback_port <= 0) {
        printf("Failed to choose a fallback UI port\n");
        return false;
    }

    char port_text[32];
    snprintf(port_text, sizeof(port_text), "%d", fallback_port);
    String backend_id = String("org.elara.ui.elara-os.p") + String(fallback_port);

    pid_t pid = fork();
    if (pid < 0) {
        printf("Failed to fork elaraui-server launcher: %s\n", strerror(errno));
        return false;
    }
    if (pid == 0) {
        execlp("elaraui-server", "elaraui-server",
               "--port", port_text,
               "--backend-id", backend_id.operator char *(),
               "--persistent",
               (char *)NULL);
        fprintf(stderr, "Failed to exec elaraui-server: %s\n", strerror(errno));
        _exit(127);
    }

    owned_ui_server_pid = pid;
    recordLaunchedPid("elara-os-ui-head", pid);
    host = String("127.0.0.1");
    port = fallback_port;
    printf("Spawned elaraui-server pid=%d on fallback port %d backend_id=%s\n",
           (int)pid,
           port,
           backend_id.operator char *());

    for (int attempt = 0; attempt < 40; ++attempt) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            owned_ui_server_pid = -1;
            if (WIFEXITED(status)) {
                printf("elaraui-server exited before accepting connections with code %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("elaraui-server exited before accepting connections from signal %d\n", WTERMSIG(status));
            } else {
                printf("elaraui-server exited before accepting connections with status 0x%x\n", status);
            }
            return false;
        }
        usleep(100000);
        if (peer->connect(host, (unsigned short)port)) {
            return true;
        }
    }

    printf("Timed out waiting for fallback elaraui-server on %s:%d\n", host.operator char *(), port);
    stopOwnedUiServer();
    return false;
}

bool ElaraOsApp::connectUiPeer() {
    if (prefer_owned_ui_server) {
        printf("Launching dedicated elaraui-server for Elara OS\n");
        return launchUiServerFallback();
    }
    if (peer->connect(host, (unsigned short)port)) {
        return true;
    }
    printf("Failed to connect to %s:%d\n", host.operator char *(), port);
    return launchUiServerFallback();
}

bool ElaraOsApp::ensureDirectoryPath(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    std::string partial;
    for (size_t i = 0; i < path.size(); ++i) {
        partial += path[i];
        if (path[i] != '/' || partial.size() <= 1) {
            continue;
        }
        if (mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
            printf("mkdir failed for %s: %s\n", partial.c_str(), strerror(errno));
            return false;
        }
    }
    if (mkdir(path.c_str(), 0775) != 0 && errno != EEXIST) {
        printf("mkdir failed for %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    return true;
}

bool ElaraOsApp::ensureFileContents(const std::string &path, const std::string &contents) {
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (fd < 0) {
        printf("open failed for %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    const char *data = contents.c_str();
    size_t remaining = contents.size();
    while (remaining > 0) {
        ssize_t written = write(fd, data, remaining);
        if (written <= 0) {
            printf("write failed for %s: %s\n", path.c_str(), strerror(errno));
            close(fd);
            return false;
        }
        data += written;
        remaining -= (size_t)written;
    }
    close(fd);
    return true;
}

bool ElaraOsApp::bootstrapVirtualDrives() {
    const std::string root = virtual_drive_root;
    if (!ensureDirectoryPath(root)) {
        return false;
    }

    if (!ensureFileContents(
            root + "/manifest.json",
            "{\n"
            "  \"drives\": [\n"
            "    {\"drive_id\": 1, \"path\": \"root\", \"role\": \"system\", \"partition_table\": \"gpt\"},\n"
            "    {\"drive_id\": 2, \"path\": \"data\", \"role\": \"data\", \"partition_table\": \"gpt\"}\n"
            "  ],\n"
            "  \"filesystem_authority\": \"elara.os.filesystem\",\n"
            "  \"block_io_authority\": \"elara.os.block_io\",\n"
            "  \"mount_policy\": \"derive-from-partitions\"\n"
            "}\n") ||
        !ensureFileContents(
            root + "/README.txt",
            "Elara OS virtual block images live in this directory.\n"
            "Mounts are not declared here.\n"
            "Partition tables are created on the raw images and mount policy is derived later.\n")) {
        return false;
    }

    return true;
}

bool ElaraOsApp::connectEpaDbg() {
    std::lock_guard<std::mutex> lock(epa_dbg_mutex);
    if (epa_dbg_fd >= 0) {
        return true;
    }
    refreshDebugSessionConfigFromEnv();
    if (!epa_dbg_host.length() || epa_dbg_port <= 0) {
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_text[32];
    snprintf(port_text, sizeof(port_text), "%d", epa_dbg_port);
    struct addrinfo *result = NULL;
    if (getaddrinfo(epa_dbg_host.operator char *(), port_text, &hints, &result) != 0) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *it = result; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    epa_dbg_fd = fd;
    if (epa_dbg_fd >= 0) {
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(epa_dbg_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(epa_dbg_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        printf("[C++ Host] connected to EPA debug VM at %s:%d\n", epa_dbg_host.operator char *(), epa_dbg_port);
    }
    return epa_dbg_fd >= 0;
}

bool ElaraOsApp::refreshDebugSessionConfigFromEnv() {
    const char *session_path = getenv("ELARA_DEBUG_SESSION");
    std::string text = readTextFile(session_path);
    if (session_path && session_path[0]) {
        std::string path(session_path);
        std::string::size_type slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            std::string latest_path = path.substr(0, slash + 1u) + "latest.json";
            std::string latest_text = readTextFile(latest_path.c_str());
            if (!latest_text.empty()) {
                text = latest_text;
            }
        }
    }
    if (text.empty()) {
        return false;
    }

    std::string session_epa_host = jsonStringField(text, "epa_dbg_host");
    int session_epa_port = jsonIntField(text, "epa_dbg_port", 0);
    std::string session_host_debug_host = jsonStringField(text, "host_debug_host");
    int session_host_debug_port = jsonIntField(text, "host_debug_port", 0);
    std::string session_bundle_path = jsonStringField(text, "bundle_path");

    if (session_epa_host.size()) {
        epa_dbg_host = String(session_epa_host.c_str());
    }
    if (session_host_debug_host.size()) {
        host_bridge_host = String(session_host_debug_host.c_str());
    }
    if (session_host_debug_port > 0) {
        host_bridge_port = session_host_debug_port;
    }
    if (session_epa_port > 0 && session_epa_port != epa_dbg_port) {
        epa_dbg_port = session_epa_port;
        if (epa_dbg_fd >= 0) {
            close(epa_dbg_fd);
            epa_dbg_fd = -1;
        }
    }
    if (session_bundle_path.size()) {
        bundle_path = String(session_bundle_path.c_str());
    }
    return session_epa_port > 0;
}

void ElaraOsApp::closeEpaDbg() {
    std::lock_guard<std::mutex> lock(epa_dbg_mutex);
    if (epa_dbg_fd >= 0) {
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
    }
}

bool ElaraOsApp::epaDbgCall(const String &method, const String &params_json, String &result_json) {
    epa_dbg_last_error = String("");
    if (epa_dbg_port <= 0) {
        if (!local_epa_debug_service) {
            local_epa_debug_service = Ref<ElaraOsEpaDebugService>(new ElaraOsEpaDebugService());
            printf("[C++ Host] using in-process EPA debug service\n");
        }
        String error_code;
        String error_message;
        if (local_epa_debug_service->call(method, params_json.length() ? params_json : String("{}"), result_json, error_code, error_message)) {
            return true;
        }
        printf("[C++ Host] local EPA debug call failed method=%s code=%s msg=%s\n",
               String(method).operator char *(),
               error_code.operator char *(),
               error_message.operator char *());
        epa_dbg_last_error = String("local EPA debug call failed method=") + method
            + String(" code=") + error_code
            + String(" msg=") + error_message;
        return false;
    }

    if (!connectEpaDbg()) {
        String method_copy(method);
        printf("[C++ Host] EPA debug connect failed method=%s\n", method_copy.operator char *());
        epa_dbg_last_error = String("EPA debug connect failed method=") + method;
        return false;
    }

    std::lock_guard<std::mutex> lock(epa_dbg_mutex);
    String method_copy(method);
    ByteArray request = BRpcRpcCodec::buildRequest(
        String("elara-os-host"),
        method,
        params_json.length() ? params_json : String("{}")
    );
    ByteArray frame = BRpcRpcCodec::framePayload(request);
    if (write(epa_dbg_fd, frame.operator const char *(), (size_t)frame.length()) != (ssize_t)frame.length()) {
        printf("[C++ Host] EPA debug write failed method=%s\n", method_copy.operator char *());
        epa_dbg_last_error = String("EPA debug write failed method=") + method;
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }

    unsigned char rhdr[4];
    if (!readExact(epa_dbg_fd, (char *)rhdr, 4)) {
        printf("[C++ Host] EPA debug read header failed method=%s\n", method_copy.operator char *());
        epa_dbg_last_error = String("EPA debug read header failed method=") + method;
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    uint32_t rlen = ((uint32_t)rhdr[0] << 24) | ((uint32_t)rhdr[1] << 16)
                  | ((uint32_t)rhdr[2] << 8)  | (uint32_t)rhdr[3];
    if (rlen > 4u * 1024u * 1024u) {
        epa_dbg_last_error = String("EPA debug response too large method=") + method;
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    std::vector<char> response(rlen + 1, 0);
    if (!readExact(epa_dbg_fd, response.data(), rlen)) {
        printf("[C++ Host] EPA debug read body failed method=%s\n", method_copy.operator char *());
        epa_dbg_last_error = String("EPA debug read body failed method=") + method;
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }

    String response_id;
    bool ok = false;
    String error_code;
    String error_message;
    String parse_error;
    if (!BRpcRpcCodec::parseResponse(
            response.data(),
            (size_t)rlen,
            response_id,
            ok,
            result_json,
            error_code,
            error_message,
            parse_error)) {
        printf("[C++ Host] EPA debug response parse failed: %s\n", parse_error.operator char *());
        epa_dbg_last_error = String("EPA debug response parse failed method=") + method
            + String(" msg=") + parse_error;
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    if (!ok) {
        printf("[C++ Host] EPA debug RPC failed method=%s code=%s msg=%s\n",
               method_copy.operator char *(),
               error_code.operator char *(),
               error_message.operator char *());
        epa_dbg_last_error = String("EPA debug RPC failed method=") + method
            + String(" code=") + error_code
            + String(" msg=") + error_message;
        return false;
    }
    return true;
}

bool ElaraOsApp::epaDbgLoadBundle() {
    if (epa_loaded) {
        return true;
    }
    if (!bundle_path.length()) {
        return false;
    }
    String result_json;
    String params = String("{\"bundle_path\":") + jsonQuoteString(bundle_path) + String("}");
    sendHostDebugEvent("state", "\"status\":\"loading EPA bundle for boot descriptor ingress\"");
    if (!epaDbgCall(String("epa.debug.loadBundle"), params, result_json)) {
        return false;
    }
    epa_loaded = true;
    sendHostDebugEvent("state", "\"status\":\"EPA bundle loaded for boot descriptor ingress\"");
    return true;
}

bool ElaraOsApp::ingressBootDescriptor(const String &payload_hex, String &result_json, String &error_message) {
    if (!payload_hex.length()) {
        error_message = String("missing payload_hex");
        return false;
    }
    if (!epaDbgLoadBundle()) {
        error_message = String("failed to load EPA bundle before boot ingress");
        return false;
    }

    String path_id("boot");
    String ingress_result;
    String ingress_params = String("{\"path_id\":") + jsonQuoteString(path_id)
        + String(",\"wid\":1,\"payload_hex\":") + jsonQuoteString(payload_hex) + String("}");
    sendHostDebugEvent("state", "\"status\":\"pushing boot descriptor into EPA ingress\"");
    if (!epaDbgCall(String("epa.debug.ingressPushHex"), ingress_params, ingress_result)) {
        error_message = String("epa.debug.ingressPushHex failed");
        return false;
    }

    sendHostDebugEvent(
        "ingress",
        "\"kernel\":\"elara.os.boot\",\"worker\":\"wid=1\",\"type\":\"BootDeviceList\",\"details\":\"hardware descriptor payload queued\""
    );

    pending_boot_payload_hex = payload_hex;
    boot_payload_pending = true;
    result_json = String("{\"queued\":true,\"path_id\":\"boot\",\"payload_bytes\":")
        + String((int)(payload_hex.length() / 2))
        + String(",\"ingress\":") + (ingress_result.length() ? ingress_result : String("{}"))
        + String(",\"run_pending\":true}")
        ;
    sendHostDebugEvent("state", "\"status\":\"boot descriptor queued in EPA ingress\"");    
    return true;
}

bool ElaraOsApp::continueBootDescriptor(String &result_json, String &error_message) {
    if (!boot_payload_pending || !pending_boot_payload_hex.length()) {
        error_message = String("no queued boot descriptor to continue");
        return false;
    }

    String boot_path_id("boot");
    String entry_path_id("entry");
    String registry_path_id("registry_authority");
    String block_io_path_id("block_io_authority");
    String partition_io_path_id("partition_io_authority");
    String filesystem_path_id("filesystem_authority");
    String frame_path_id("frame_io_authority");
    String run_result;
    String clear_result;
    String entry_run_result;
    String registry_run_result;
    String registry_partition_run_result;
    String registry_mount_run_result;
    String block_io_run_result;
    String block_io_read_run_result;
    String block_io_mailbox_result;
    String block_io_response_ingress_result;
    String partition_io_run_result;
    String boot_kernel_ingress_result;
    String boot_kernel_run_result;
    String boot_walk_ingress_result;
    String boot_walk_payload_hex_result;
    String filesystem_run_result;
    String filesystem_mailbox_result;
    String filesystem_response_mailbox_result;
    String block_io_mailbox_before_pump_result;
    String block_io_device_read_result("{}");
    String entry_post_block_run_result;
    String registry_post_block_run_result;
    String frame_run_result;
    String mailbox_result;
    String frame_json;
    String root_ext4_json("{}");
    String root_inode_json("{}");
    String root_dir_json("[]");
    String kernel_image_json("{}");
    epaDbgCall(String("epa.debug.clearMailbox"), String("{\"path_id\":\"frame_io_authority\"}"), clear_result);
    sendHostDebugEvent("state", "\"status\":\"running EPA after queued boot descriptor\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(boot_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            run_result)) {
        error_message = String("epa.debug.run failed after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Dynamic ACL Authority registration worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(entry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            entry_run_result)) {
        error_message = String("epa.debug.run failed for entry after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Registry Authority registration worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(registry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            registry_run_result)) {
        error_message = String("epa.debug.run failed for registry_authority after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    std::vector<BootDriveInfo> boot_drives = parseBootDriveInfos(pending_boot_descriptor_params_json);
    if (boot_drives.empty()) {
        const char *names[2] = {"root", "data"};
        for (int i = 0; i < 2; ++i) {
            std::string drive_path = virtual_drive_root + "/" + names[i];
            struct stat st;
            if (stat(drive_path.c_str(), &st) != 0 || st.st_size <= 0) {
                continue;
            }
            BootDriveInfo drive;
            drive.drive_id = i + 1;
            drive.path = drive_path;
            drive.flags = (i == 0) ? 1 : 0;
            drive.block_size = 512;
            drive.block_count = (int)(st.st_size / 512);
            boot_drives.push_back(drive);
        }
    }
    bool root_mount_registered = false;
    uint32_t root_mount_path_hash = hashU32Literal("/");
    std::vector<GptPartitionInfo> discovered_partitions;
    for (size_t i = 0; i < boot_drives.size(); ++i) {
        std::string payload_hex;
        String ingress_result;
        std::vector<GptPartitionInfo> partitions;
        std::string scan_error;

        appendLeU32Hex(payload_hex, (uint32_t)boot_drives[i].drive_id);
        appendLeU32Hex(payload_hex, (uint32_t)boot_drives[i].block_size);
        appendLeU32Hex(payload_hex, (uint32_t)boot_drives[i].block_count);
        appendLeU32Hex(payload_hex, (uint32_t)boot_drives[i].flags);
        if (!epaDbgCall(
                String("epa.debug.ingressPushHex"),
                String("{\"path_id\":") + jsonQuoteString(block_io_path_id)
                    + String(",\"wid\":1,\"payload_hex\":") + jsonQuoteString(String(payload_hex.c_str())) + String("}"),
                ingress_result)) {
            error_message = String("failed to queue block device registration");
            return false;
        }

        if (!scanGptPartitions(boot_drives[i], partitions, scan_error)) {
            error_message = String(scan_error.c_str());
            return false;
        }

        for (size_t p = 0; p < partitions.size(); ++p) {
            discovered_partitions.push_back(partitions[p]);
            std::string partition_hex;
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].drive_id);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].partition_drive_id);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].partition_index);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].first_lba);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].last_lba);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].fs_kind);
            appendLeU32Hex(partition_hex, (uint32_t)partitions[p].flags);
            if (!epaDbgCall(
                    String("epa.debug.ingressPushHex"),
                    String("{\"path_id\":") + jsonQuoteString(partition_io_path_id)
                        + String(",\"wid\":2,\"payload_hex\":") + jsonQuoteString(String(partition_hex.c_str())) + String("}"),
                    ingress_result)) {
                    error_message = String("failed to queue partition registration");
                    return false;
            }

            std::string registry_partition_hex;
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].drive_id);
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].partition_drive_id);
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].first_lba);
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].last_lba);
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].fs_kind);
            appendLeU32Hex(registry_partition_hex, (uint32_t)partitions[p].flags);
            if (!epaDbgCall(
                    String("epa.debug.ingressPushHex"),
                    String("{\"path_id\":") + jsonQuoteString(registry_path_id)
                        + String(",\"wid\":2,\"payload_hex\":") + jsonQuoteString(String(registry_partition_hex.c_str())) + String("}"),
                    ingress_result)) {
                error_message = String("failed to queue registry partition registration");
                return false;
            }

            char partition_payload[512];
            snprintf(
                partition_payload,
                sizeof(partition_payload),
                "\"status\":\"gpt partition detected\",\"drive_id\":%d,\"partition_drive_id\":%d,\"partition_index\":%d,\"first_lba\":%llu,\"last_lba\":%llu,\"name\":\"%s\",\"flags\":%d",
                partitions[p].drive_id,
                partitions[p].partition_drive_id,
                partitions[p].partition_index,
                (unsigned long long)partitions[p].first_lba,
                (unsigned long long)partitions[p].last_lba,
                jsonEscape(partitions[p].name.c_str()).c_str(),
                partitions[p].flags
            );
            sendHostDebugEvent("state", partition_payload);

            if (!root_mount_registered && partitions[p].flags == 1) {
                std::string mount_hex;
                std::vector<unsigned char> ext4_superblock;
                Ext4RootInodeSummary root_inode_summary;
                std::vector<Ext4DirectoryEntrySummary> root_dir_entries;
                std::vector<unsigned char> kernel_image;
                std::string read_error;
                bool kernel_image_loaded = false;
                if (!readPartitionBytes(boot_drives[i], partitions[p], 1024u, 1024u, ext4_superblock, read_error)) {
                    error_message = String(read_error.c_str());
                    return false;
                }
                root_ext4_json = ext4SuperblockSummaryJson(partitions[p], ext4_superblock);
                appendLeU32Hex(mount_hex, 1u);
                appendLeU32Hex(mount_hex, (uint32_t)partitions[p].partition_drive_id);
                appendLeU32Hex(mount_hex, 1u);
                appendLeU32Hex(mount_hex, 3u);
                appendLeU32Hex(mount_hex, root_mount_path_hash);
                uint32_t log_block_size = readLeU32At(ext4_superblock.data() + 24u);
                uint32_t block_size = 1024u;
                for (uint32_t bi = 0; bi < log_block_size && bi < 16u; ++bi) {
                    block_size *= 2u;
                }
                uint32_t inode_size = (uint32_t)readLeU16At(ext4_superblock.data() + 88u);
                if (!readExt4RootInodeSummary(boot_drives[i], partitions[p], block_size, inode_size, root_inode_summary, read_error)) {
                    error_message = String(read_error.c_str());
                    return false;
                }
                root_inode_json = ext4RootInodeSummaryJson(root_inode_summary);
                if (!readExt4RootDirectoryEntries(boot_drives[i], partitions[p], block_size, root_inode_summary, root_dir_entries, read_error)) {
                    error_message = String(read_error.c_str());
                    return false;
                }
                root_dir_json = ext4DirectoryEntriesSummaryJson(root_dir_entries);
                if (!readExt4KernelImage(
                        boot_drives[i],
                        partitions[p],
                        block_size,
                        inode_size,
                        readLeU32At(ext4_superblock.data() + 40u),
                        kernel_image,
                        read_error)) {
                    kernel_image_json = String("{\"path\":\"/boot/elara/epa_kernel.bin\",\"error\":\"")
                        + String(jsonEscape(read_error.c_str()).c_str())
                        + String("\"}");
                    sendHostDebugEvent(
                        "state",
                        (String("\"status\":\"second-stage kernel image read deferred\",\"error\":\"")
                            + String(jsonEscape(read_error.c_str()).c_str())
                            + String("\"")).operator char *()
                    );
                } else {
                    kernel_image_loaded = true;
                    kernel_image_json = String("{\"path\":\"/boot/elara/epa_kernel.bin\",\"bytes\":")
                        + String((int)kernel_image.size()) + String("}");
                }
                appendLeU32Hex(mount_hex, (uint32_t)readLeU16At(ext4_superblock.data() + 56u));
                appendLeU32Hex(mount_hex, block_size);
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 4u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 0u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 32u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 40u));
                appendLeU32Hex(mount_hex, (uint32_t)readLeU16At(ext4_superblock.data() + 88u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 92u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 96u));
                appendLeU32Hex(mount_hex, readLeU32At(ext4_superblock.data() + 100u));
                appendLeU32Hex(mount_hex, root_inode_summary.mode);
                appendLeU32Hex(mount_hex, root_inode_summary.size_lo);
                appendLeU32Hex(mount_hex, root_inode_summary.blocks_lo);
                appendLeU32Hex(mount_hex, root_inode_summary.flags);
                appendLeU32Hex(mount_hex, root_inode_summary.extent_magic);
                appendLeU32Hex(mount_hex, root_inode_summary.extent_entries);
                appendLeU32Hex(mount_hex, root_inode_summary.extent_depth);
                if (!epaDbgCall(
                        String("epa.debug.ingressPushHex"),
                        String("{\"path_id\":") + jsonQuoteString(filesystem_path_id)
                            + String(",\"wid\":1,\"payload_hex\":") + jsonQuoteString(String(mount_hex.c_str())) + String("}"),
                        ingress_result)) {
                    error_message = String("failed to queue root mount registration");
                    return false;
                }
                if (!epaDbgCall(
                        String("epa.debug.run"),
                        String("{\"path_id\":") + jsonQuoteString(filesystem_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
                        filesystem_run_result)) {
                    error_message = String("epa.debug.run failed while staging root mount before directory entries");
                    if (epa_dbg_last_error.length()) {
                        error_message = error_message + String(": ") + epa_dbg_last_error;
                    }
                    return false;
                }

                for (int boot_entry_pass = 0; boot_entry_pass < 2; ++boot_entry_pass) {
                for (size_t di = 0; di < root_dir_entries.size(); ++di) {
                    bool is_boot_entry = root_dir_entries[di].name == "boot";
                    if ((boot_entry_pass == 0 && is_boot_entry) || (boot_entry_pass == 1 && !is_boot_entry)) {
                        continue;
                    }
                    std::string directory_entry_hex;
                    appendLeU32Hex(directory_entry_hex, 1u);
                    appendLeU32Hex(directory_entry_hex, 2u);
                    appendLeU32Hex(directory_entry_hex, root_dir_entries[di].inode_number);
                    appendLeU32Hex(directory_entry_hex, root_dir_entries[di].name_hash);
                    appendLeU32Hex(directory_entry_hex, root_dir_entries[di].file_type);
                    appendLeU32Hex(directory_entry_hex, root_dir_entries[di].name_len);
                    appendLeU32Hex(directory_entry_hex, root_dir_entries[di].rec_len);
                    if (!epaDbgCall(
                            String("epa.debug.ingressPushHex"),
                            String("{\"path_id\":") + jsonQuoteString(filesystem_path_id)
                                + String(",\"wid\":6,\"payload_hex\":") + jsonQuoteString(String(directory_entry_hex.c_str())) + String("}"),
                            ingress_result)) {
                        error_message = String("failed to queue root directory entry registration");
                        return false;
                    }
                    if (is_boot_entry) {
                        std::string boot_walk_hex;
                        appendLeU32Hex(boot_walk_hex, 2u);
                        appendLeU32Hex(boot_walk_hex, (uint32_t)partitions[p].partition_drive_id);
                        appendLeU32Hex(boot_walk_hex, root_dir_entries[di].inode_number);
                        appendLeU32Hex(boot_walk_hex, block_size);
                        appendLeU32Hex(boot_walk_hex, 0u);
                        boot_walk_payload_hex_result = String(boot_walk_hex.c_str());
                        if (!epaDbgCall(
                                String("epa.debug.ingressPushHex"),
                                String("{\"path_id\":") + jsonQuoteString(filesystem_path_id)
                                    + String(",\"wid\":2,\"payload_hex\":") + jsonQuoteString(String(boot_walk_hex.c_str())) + String("}"),
                                boot_walk_ingress_result)) {
                            error_message = String("failed to queue filesystem boot inode walk");
                            return false;
                        }
                    }
                }
                }

                std::string registry_mount_hex;
                appendLeU32Hex(registry_mount_hex, 1u);
                appendLeU32Hex(registry_mount_hex, (uint32_t)partitions[p].partition_drive_id);
                appendLeU32Hex(registry_mount_hex, 1u);
                appendLeU32Hex(registry_mount_hex, 3u);
                appendLeU32Hex(registry_mount_hex, root_mount_path_hash);
                if (!epaDbgCall(
                        String("epa.debug.ingressPushHex"),
                        String("{\"path_id\":") + jsonQuoteString(registry_path_id)
                            + String(",\"wid\":3,\"payload_hex\":") + jsonQuoteString(String(registry_mount_hex.c_str())) + String("}"),
                        ingress_result)) {
                    error_message = String("failed to queue registry mount registration");
                    return false;
                }
                root_mount_registered = true;
                if (kernel_image_loaded) {
                    std::string kernel_hex;
                    appendBytesHex(kernel_hex, kernel_image);
                    if (!epaDbgCall(
                            String("epa.debug.ingressPushHex"),
                            String("{\"path_id\":") + jsonQuoteString(boot_path_id)
                                + String(",\"wid\":2,\"payload_hex\":") + jsonQuoteString(String(kernel_hex.c_str())) + String("}"),
                            boot_kernel_ingress_result)) {
                        error_message = String("failed to queue second-stage kernel image");
                        return false;
                    }
                    if (!epaDbgCall(
                            String("epa.debug.run"),
                            String("{\"path_id\":") + jsonQuoteString(boot_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
                            boot_kernel_run_result)) {
                        error_message = String("epa.debug.run failed for boot kernel image handoff");
                        if (epa_dbg_last_error.length()) {
                            error_message = error_message + String(": ") + epa_dbg_last_error;
                        }
                        return false;
                    }
                }
                sendHostDebugEvent(
                    "state",
                    (String("\"status\":\"root partition mounted and registered\",\"mount\":\"/\",\"partition_drive_id\":")
                        + String(partitions[p].partition_drive_id)
                        + String(",\"mount_path_hash\":")
                        + String((int)root_mount_path_hash)).operator char *()
                );
            }
        }
    }

    sendHostDebugEvent("state", "\"status\":\"running Block IO Authority registration worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(block_io_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            block_io_run_result)) {
        error_message = String("epa.debug.run failed for block_io_authority after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Partition IO Authority scan worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(partition_io_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            partition_io_run_result)) {
        error_message = String("epa.debug.run failed for partition_io_authority after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Registry Authority partition registration worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(registry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            registry_partition_run_result)) {
        error_message = String("epa.debug.run failed for registry_authority partition registration");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Filesystem Authority mount worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(filesystem_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            filesystem_run_result)) {
        error_message = String("epa.debug.run failed for filesystem_authority after GPT scan");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }
    epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"filesystem_authority\"}"), filesystem_mailbox_result);
    epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"block_io_authority\"}"), block_io_mailbox_before_pump_result);

    sendHostDebugEvent("state", "\"status\":\"servicing EPA block I/O read requests\"");
    int block_io_serviced_count = 0;
    for (int block_io_pump_iter = 0; block_io_pump_iter < 16; ++block_io_pump_iter) {
        String clear_block_mailbox_result;
        epaDbgCall(String("epa.debug.clearMailbox"), String("{\"path_id\":\"block_io_authority\"}"), clear_block_mailbox_result);
        if (!epaDbgCall(
                String("epa.debug.run"),
                String("{\"path_id\":") + jsonQuoteString(block_io_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
                block_io_read_run_result) ||
            !epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"block_io_authority\"}"), block_io_mailbox_result)) {
            block_io_device_read_result = String("{\"serviced\":")
                + String(block_io_serviced_count)
                + String(",\"error\":\"block_io run or mailbox read failed\"}");
            break;
        }

        std::string mailbox_json(block_io_mailbox_result.operator char *() ? block_io_mailbox_result.operator char *() : "");
        std::string mailbox_hex = jsonStringField(mailbox_json, "hex");
        int mailbox_len = jsonIntField(mailbox_json, "len", 0);
        std::vector<unsigned char> mailbox_bytes;
        if (mailbox_len < ORANGE_FORTRESS_EPA_FRAME_HEADER_BYTES ||
            !decodeHexBytes(mailbox_hex, mailbox_bytes) ||
            (int)mailbox_bytes.size() != mailbox_len) {
            if (block_io_serviced_count > 0 && block_io_device_read_result.length()) {
                block_io_device_read_result =
                    String("{\"serviced\":") + String(block_io_serviced_count)
                    + String(",\"done\":true,\"reason\":\"no block request\",\"mailbox_len\":")
                    + String(mailbox_len)
                    + String(",\"previous\":") + block_io_device_read_result
                    + String("}");
            } else {
                block_io_device_read_result =
                    String("{\"serviced\":") + String(block_io_serviced_count)
                    + String(",\"done\":true,\"reason\":\"no block request\",\"mailbox_len\":")
                    + String(mailbox_len) + String("}");
            }
            break;
        }

        ElaraOsEpaFrameHeader block_header = orangeFortressParseEgressFrameHeader(mailbox_bytes.data(), mailbox_bytes.size());
        if (!block_header.valid || block_header.width != 7253218u || block_header.frame_type != 1u) {
            block_io_device_read_result =
                String("{\"serviced\":") + String(block_io_serviced_count)
                + String(",\"done\":true,\"reason\":\"mailbox was not a block read request\",\"mailbox_len\":")
                + String(mailbox_len) + String("}");
            break;
        }

        int request_drive_id = (int)(block_header.height & 0xffffu);
        int request_target_inode = (int)((block_header.height >> 16) & 0xffffu);
        int request_lba = (int)block_header.frame_id;
        int request_block_count = (int)(block_header.record_count & 0xffu);
        int request_phase = (int)((block_header.record_count >> 8) & 0xffu);
        int request_block_size = request_block_count * 512;
        int request_inode_table_block = (int)(block_header.record_count >> 16);
        if ((request_phase == 2 || request_phase == 5) && request_target_inode > 0 && request_block_size > 0) {
            if (request_inode_table_block == 0) {
                int inode_index = request_target_inode - 1;
                uint64_t inode_table_byte_offset = ((uint64_t)request_lba * 512ull) - ((uint64_t)inode_index * 256ull);
                request_inode_table_block = (int)(inode_table_byte_offset / (uint64_t)request_block_size);
            }
        }
        const BootDriveInfo *source_drive = NULL;
        const GptPartitionInfo *source_partition = NULL;
        for (size_t pi = 0; pi < discovered_partitions.size(); ++pi) {
            if (discovered_partitions[pi].partition_drive_id == request_drive_id) {
                source_partition = &discovered_partitions[pi];
                for (size_t di = 0; di < boot_drives.size(); ++di) {
                    if (boot_drives[di].drive_id == discovered_partitions[pi].drive_id) {
                        source_drive = &boot_drives[di];
                        break;
                    }
                }
                break;
            }
        }
        if (!source_drive || !source_partition || request_block_count <= 0 || request_block_count > 8) {
            block_io_device_read_result =
                String("{\"serviced\":") + String(block_io_serviced_count)
                + String(",\"error\":\"no matching partition or invalid block count\",\"drive_id\":")
                + String(request_drive_id)
                + String(",\"lba\":") + String(request_lba)
                + String(",\"block_count\":") + String(request_block_count)
                + String("}");
            break;
        }

        std::vector<unsigned char> block_payload;
        std::string read_error;
        uint64_t byte_offset = (uint64_t)request_lba * 512ull;
        uint64_t byte_count = (uint64_t)request_block_count * 512ull;
        if (!readPartitionBytes(*source_drive, *source_partition, byte_offset, byte_count, block_payload, read_error)) {
            block_io_device_read_result = String("{\"serviced\":")
                + String(block_io_serviced_count)
                + String(",\"error\":\"")
                + String(jsonEscape(read_error.c_str()).c_str())
                + String("\"}");
            break;
        }
        uint32_t response_arg0 = 0u;
        uint32_t response_arg1 = 0u;
        if (request_phase == 1 && block_payload.size() >= 12u) {
            response_arg0 = readLeU32At(block_payload.data() + 8u);
        } else if ((request_phase == 2 || request_phase == 5) && request_target_inode > 0 && request_block_size > 0) {
            uint64_t inode_index = (uint64_t)(request_target_inode - 1);
            uint64_t inode_table_byte_offset = (uint64_t)request_inode_table_block * (uint64_t)request_block_size;
            uint64_t inode_byte_offset = inode_table_byte_offset + (inode_index * 256ull);
            uint64_t inode_payload_offset = inode_byte_offset - ((uint64_t)request_lba * 512ull);
            if (inode_payload_offset + 64ull <= block_payload.size()) {
                response_arg0 = (uint32_t)readLeU16At(block_payload.data() + inode_payload_offset + 40ull);
                response_arg1 = readLeU32At(block_payload.data() + inode_payload_offset + 60ull);
            }
        }

        std::string response_hex;
        appendLeU32Hex(response_hex, 2u);
        appendLeU32Hex(response_hex, (uint32_t)request_drive_id);
        appendLeU32Hex(response_hex, (uint32_t)request_lba);
        appendLeU32Hex(response_hex, (uint32_t)request_block_count);
        appendLeU32Hex(response_hex, (uint32_t)block_payload.size());
        appendLeU32Hex(response_hex, (uint32_t)request_phase);
        appendLeU32Hex(response_hex, (uint32_t)request_target_inode);
        appendLeU32Hex(response_hex, (uint32_t)request_block_size);
        appendLeU32Hex(response_hex, (uint32_t)request_inode_table_block);
        appendLeU32Hex(response_hex, response_arg0);
        appendLeU32Hex(response_hex, response_arg1);
        appendBytesHex(response_hex, block_payload);
        if (!epaDbgCall(
                String("epa.debug.ingressPushHex"),
                String("{\"path_id\":") + jsonQuoteString(filesystem_path_id)
                    + String(",\"wid\":4,\"payload_hex\":") + jsonQuoteString(String(response_hex.c_str())) + String("}"),
                block_io_response_ingress_result)) {
            block_io_device_read_result = String("{\"serviced\":")
                + String(block_io_serviced_count)
                + String(",\"error\":\"failed to inject block response\"}");
            break;
        }

        String filesystem_block_result_run;
        epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(filesystem_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            filesystem_block_result_run);
        epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"filesystem_authority\"}"), filesystem_response_mailbox_result);
        block_io_serviced_count = block_io_serviced_count + 1;
        block_io_device_read_result =
            String("{\"serviced\":") + String(block_io_serviced_count)
            + String(",\"last_drive_id\":") + String(request_drive_id)
            + String(",\"last_lba\":") + String(request_lba)
            + String(",\"last_block_count\":") + String(request_block_count)
            + String(",\"last_bytes\":") + String((int)block_payload.size())
            + String(",\"last_ingress\":") + (block_io_response_ingress_result.length() ? block_io_response_ingress_result : String("{}"))
            + String(",\"last_filesystem_mailbox\":") + (filesystem_response_mailbox_result.length() ? filesystem_response_mailbox_result : String("{}"))
            + String(",\"last_filesystem_run\":") + (filesystem_block_result_run.length() ? filesystem_block_result_run : String("{}"))
            + String("}");
    }

    sendHostDebugEvent("state", "\"status\":\"running Registry Authority mount registration worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(registry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            registry_mount_run_result)) {
        error_message = String("epa.debug.run failed for registry_authority mount registration");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Dynamic ACL Authority after Block IO registration\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(entry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            entry_post_block_run_result)) {
        error_message = String("epa.debug.run failed for entry after block_io_authority run");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Registry Authority after Block IO registration\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(registry_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            registry_post_block_run_result)) {
        error_message = String("epa.debug.run failed for registry_authority after block_io_authority run");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    sendHostDebugEvent("state", "\"status\":\"running Frame IO Authority boot worker\"");
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(frame_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
            frame_run_result)) {
        error_message = String("epa.debug.run failed for frame_io_authority after boot ingress");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    if (!epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"frame_io_authority\"}"), mailbox_result)) {
        error_message = String("epa.debug.getMailbox failed after frame_io_authority run");
        if (epa_dbg_last_error.length()) {
            error_message = error_message + String(": ") + epa_dbg_last_error;
        }
        return false;
    }

    if (!updateSurfaceCommandsFromMailbox(mailbox_result, frame_json, error_message)) {
        String fallback_ingress;
        String fallback_run;
        String fallback_mailbox;
        String fallback_params = String("{\"path_id\":\"frame_io_authority\",\"wid\":1,\"payload_hex\":\"0100000000000000\"}");
        sendHostDebugEvent("state", "\"status\":\"Frame IO Authority mailbox empty; injecting fallback boot frame\"");
        if (epaDbgCall(String("epa.debug.ingressPushHex"), fallback_params, fallback_ingress) &&
            epaDbgCall(
                String("epa.debug.run"),
                String("{\"path_id\":") + jsonQuoteString(frame_path_id) + String(",\"max_ticks\":65536,\"watchdog_ms\":100}"),
                fallback_run) &&
            epaDbgCall(String("epa.debug.getMailbox"), String("{\"path_id\":\"frame_io_authority\"}"), fallback_mailbox) &&
            updateSurfaceCommandsFromMailbox(fallback_mailbox, frame_json, error_message)) {
            mailbox_result = fallback_mailbox;
            frame_run_result = fallback_run;
        } else {
            error_message = error_message
                + String("; boot_run=") + (run_result.length() ? run_result : String("{}"))
                + String("; entry_run=") + (entry_run_result.length() ? entry_run_result : String("{}"))
                + String("; registry_run=") + (registry_run_result.length() ? registry_run_result : String("{}"))
                + String("; block_io_run=") + (block_io_run_result.length() ? block_io_run_result : String("{}"))
                + String("; partition_io_run=") + (partition_io_run_result.length() ? partition_io_run_result : String("{}"))
                + String("; registry_partition_run=") + (registry_partition_run_result.length() ? registry_partition_run_result : String("{}"))
                + String("; filesystem_run=") + (filesystem_run_result.length() ? filesystem_run_result : String("{}"))
                + String("; registry_mount_run=") + (registry_mount_run_result.length() ? registry_mount_run_result : String("{}"))
                + String("; boot_kernel_ingress=") + (boot_kernel_ingress_result.length() ? boot_kernel_ingress_result : String("{}"))
                + String("; boot_kernel_run=") + (boot_kernel_run_result.length() ? boot_kernel_run_result : String("{}"))
                + String("; entry_post_block_run=") + (entry_post_block_run_result.length() ? entry_post_block_run_result : String("{}"))
                + String("; registry_post_block_run=") + (registry_post_block_run_result.length() ? registry_post_block_run_result : String("{}"))
                + String("; frame_run=") + (frame_run_result.length() ? frame_run_result : String("{}"))
                + String("; mailbox=") + (mailbox_result.length() ? mailbox_result : String("{}"))
                + String("; fallback_ingress=") + (fallback_ingress.length() ? fallback_ingress : String("{}"))
                + String("; fallback_run=") + (fallback_run.length() ? fallback_run : String("{}"))
                + String("; fallback_mailbox=") + (fallback_mailbox.length() ? fallback_mailbox : String("{}"))
                + String("; epa_debug_last_error=") + (epa_dbg_last_error.length() ? epa_dbg_last_error : String("(none)"));
            return false;
        }
    }

    result_json = String("{\"continued\":true,\"path_id\":\"boot\",\"payload_bytes\":")
        + String((int)(pending_boot_payload_hex.length() / 2))
        + String(",\"run\":") + (run_result.length() ? run_result : String("{}"))
        + String(",\"entry_run\":") + (entry_run_result.length() ? entry_run_result : String("{}"))
        + String(",\"registry_run\":") + (registry_run_result.length() ? registry_run_result : String("{}"))
        + String(",\"block_io_run\":") + (block_io_run_result.length() ? block_io_run_result : String("{}"))
        + String(",\"partition_io_run\":") + (partition_io_run_result.length() ? partition_io_run_result : String("{}"))
        + String(",\"registry_partition_run\":") + (registry_partition_run_result.length() ? registry_partition_run_result : String("{}"))
        + String(",\"boot_walk_ingress\":") + (boot_walk_ingress_result.length() ? boot_walk_ingress_result : String("{}"))
        + String(",\"boot_walk_payload_hex\":") + jsonQuoteString(boot_walk_payload_hex_result)
        + String(",\"filesystem_run\":") + (filesystem_run_result.length() ? filesystem_run_result : String("{}"))
        + String(",\"filesystem_mailbox\":") + (filesystem_mailbox_result.length() ? filesystem_mailbox_result : String("{}"))
        + String(",\"block_io_mailbox_before_pump\":") + (block_io_mailbox_before_pump_result.length() ? block_io_mailbox_before_pump_result : String("{}"))
        + String(",\"block_io_read_run\":") + (block_io_read_run_result.length() ? block_io_read_run_result : String("{}"))
        + String(",\"block_io_mailbox\":") + (block_io_mailbox_result.length() ? block_io_mailbox_result : String("{}"))
        + String(",\"block_io_response_ingress\":") + (block_io_response_ingress_result.length() ? block_io_response_ingress_result : String("{}"))
        + String(",\"block_io_device_read\":") + block_io_device_read_result
        + String(",\"registry_mount_run\":") + (registry_mount_run_result.length() ? registry_mount_run_result : String("{}"))
        + String(",\"boot_kernel_ingress\":") + (boot_kernel_ingress_result.length() ? boot_kernel_ingress_result : String("{}"))
        + String(",\"boot_kernel_run\":") + (boot_kernel_run_result.length() ? boot_kernel_run_result : String("{}"))
        + String(",\"entry_post_block_run\":") + (entry_post_block_run_result.length() ? entry_post_block_run_result : String("{}"))
        + String(",\"registry_post_block_run\":") + (registry_post_block_run_result.length() ? registry_post_block_run_result : String("{}"))
        + String(",\"frame_run\":") + (frame_run_result.length() ? frame_run_result : String("{}"))
        + String(",\"mailbox\":") + (mailbox_result.length() ? mailbox_result : String("{}"))
        + String(",\"root_ext4\":") + root_ext4_json
        + String(",\"root_inode\":") + root_inode_json
        + String(",\"root_dir_entries\":") + root_dir_json
        + String(",\"kernel_image\":") + kernel_image_json
        + String(",\"frame\":") + (frame_json.length() ? frame_json : String("{}"))
        + String("}");
    boot_payload_pending = false;
    pending_boot_payload_hex = String("");
    sendHostDebugEvent("state", "\"status\":\"boot descriptor delivered and boot screen egressed\"");
    return true;
}

bool ElaraOsApp::handleExtLogicRequest(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("elara.os.bootDescriptor")) {
        String params_copy(params_json);
        std::string params(params_copy.operator char *() ? params_copy.operator char *() : "");
        String payload_hex(jsonStringField(params, "payload_hex").c_str());
        pending_boot_descriptor_params_json = params;
        if (ingressBootDescriptor(payload_hex, result_json, error_message)) {
            return true;
        }
        error_code = String("boot_descriptor_failed");
        return false;
    }
    if (method == String("elara.os.bootContinue")) {
        if (continueBootDescriptor(result_json, error_message)) {
            return true;
        }
        error_code = String("boot_continue_failed");
        return false;
    }
    if (method == String("ext.debug.status")) {
        refreshDebugSessionConfigFromEnv();
        result_json = String("{\"epa_loaded\":") + String(epa_loaded ? "true" : "false")
            + String(",\"epa_dbg_port\":") + String(epa_dbg_port)
            + String(",\"ext_logic_server\":") + String(ext_logic_server_fd >= 0 ? "true" : "false")
            + String(",\"boot_payload_pending\":") + String(boot_payload_pending ? "true" : "false")
            + String("}");
        return true;
    }
    error_code = String("not_found");
    error_message = String("unknown ext-logic method");
    return false;
}

void ElaraOsApp::startExtLogicServer() {
    if (ext_logic_server_fd >= 0) {
        return;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return;
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) != 0 || ::listen(fd, 1) != 0) {
        close(fd);
        return;
    }
    ext_logic_server_fd = fd;
    int port_num = (int)ntohs(addr.sin_port);
    {
        FILE *session = fopen(ext_logic_session_path.c_str(), "w");
        if (session) {
            fprintf(session,
                    "{\n"
                    "  \"ext_logic_host\": \"127.0.0.1\",\n"
                    "  \"ext_logic_port\": %d,\n"
                    "  \"host_debug_host\": \"%s\",\n"
                    "  \"host_debug_port\": %d,\n"
                    "  \"project\": \"elara-os\"\n"
                    "}\n",
                    port_num,
                    host_bridge_host.operator char *(),
                    host_bridge_port);
            fclose(session);
            printf("Elara OS ext-logic session: %s port=%d\n", ext_logic_session_path.c_str(), port_num);
        } else {
            printf("Failed to write ext-logic session %s: %s\n", ext_logic_session_path.c_str(), strerror(errno));
        }
    }
    char payload[128];
    snprintf(payload, sizeof(payload), "\"port\":%d", port_num);
    sendHostDebugEvent("ext_logic_listen", payload);
    ext_logic_thread = std::thread(&ElaraOsApp::extLogicServe, this);
    ext_logic_thread.detach();
}

void ElaraOsApp::extLogicServe() {
    while (ext_logic_server_fd >= 0) {
        int client = accept(ext_logic_server_fd, NULL, NULL);
        if (client < 0) {
            break;
        }
        std::thread([this, client]() {
            std::vector<char> frame;
            while (readBrpcFrame(client, frame)) {
                std::lock_guard<std::mutex> lock(ext_logic_request_mutex);
                dispatchExtLogicFrame(this, client, frame);
            }
            close(client);
        }).detach();
    }
}

bool ElaraOsApp::connectHostDebugBridge() {
    refreshDebugSessionConfigFromEnv();
    if (host_bridge_port <= 0 || host_bridge_fd >= 0) {
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_text[32];
    snprintf(port_text, sizeof(port_text), "%d", host_bridge_port);
    struct addrinfo *result = NULL;
    int rc = getaddrinfo(host_bridge_host.operator char *(), port_text, &hints, &result);
    if (rc != 0) {
        printf("Host debug bridge lookup failed: %s\n", gai_strerror(rc));
        return false;
    }

    int fd = -1;
    for (struct addrinfo *it = result; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        printf("Host debug bridge unavailable at %s:%d\n", host_bridge_host.operator char *(), host_bridge_port);
        return false;
    }

    host_bridge_fd = fd;
    host_bridge_running.store(true);
    host_bridge_thread = std::thread(&ElaraOsApp::hostDebugBridgeLoop, this);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "\"project\":\"elara-os\",\"pid\":%ld,\"message\":\"%s\"",
             (long)getpid(),
             "Elara OS Vulkan surface host registered");
    sendHostDebugEvent("register", payload);
    sendHostDebugEvent("state", "\"status\":\"vulkan surface host ready\",\"surface\":\"org.elara.ui.elara-os.surface\"");
    char drive_payload[1024];
    snprintf(drive_payload, sizeof(drive_payload),
             "\"status\":\"virtual drives ready\",\"root\":\"%s\",\"images\":[\"root\",\"data\"],\"block_io\":\"elara.os.block_io\",\"filesystem\":\"elara.os.filesystem\",\"mount_policy\":\"derive-from-partitions\"",
             jsonEscape(virtual_drive_root.c_str()).c_str());
    sendHostDebugEvent("state", drive_payload);
    startExtLogicServer();
    return true;
}

void ElaraOsApp::stopHostDebugBridge() {
    host_bridge_running.store(false);
    int fd = host_bridge_fd;
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
    }
    if (host_bridge_thread.joinable()) {
        host_bridge_thread.join();
    }
    if (fd >= 0) {
        close(fd);
        host_bridge_fd = -1;
    }
}

void ElaraOsApp::hostDebugBridgeLoop() {
    std::string line;
    char ch = 0;
    while (host_bridge_running.load()) {
        ssize_t got = recv(host_bridge_fd, &ch, 1, 0);
        if (got <= 0) {
            break;
        }
        if (ch == '\n') {
            if (line.find("\"ping\"") != std::string::npos) {
                std::string id = jsonStringField(line, "id");
                std::string payload;
                if (!id.empty()) {
                    payload = "\"id\":\"";
                    payload += jsonEscape(id.c_str());
                    payload += "\"";
                }
                sendHostDebugEvent("pong", payload.empty() ? NULL : payload.c_str());
            } else if (line.find("\"quit\"") != std::string::npos) {
                quit_requested.store(true);
            }
            line.clear();
        } else {
            line += ch;
            if (line.size() > 8192) {
                line.clear();
            }
        }
    }
    host_bridge_running.store(false);
}

bool ElaraOsApp::sendHostDebugEvent(const char *kind, const char *payload) {
    if (host_bridge_fd < 0) {
        return false;
    }
    std::string event = "{\"kind\":\"";
    event += jsonEscape(kind);
    event += "\",\"session_id\":\"\",\"project\":\"elara-os\"";
    if (payload && payload[0]) {
        event += ",";
        event += payload;
    }
    event += "}\n";

    std::lock_guard<std::mutex> lock(host_bridge_mutex);
    const char *data = event.c_str();
    size_t remaining = event.size();
    while (remaining > 0) {
        ssize_t sent = send(host_bridge_fd, data, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= (size_t)sent;
    }
    return true;
}

int ElaraOsApp::run() {
    printf("Elara OS virtual drive root: %s\n", virtual_drive_root.c_str());
    if (!bootstrapVirtualDrives()) {
        printf("Failed to bootstrap virtual drives under %s\n", virtual_drive_root.c_str());
        return 1;
    }
    printf("Elara OS virtual drives ready under %s\n", virtual_drive_root.c_str());
    if (!connectUiPeer()) {
        printf("Failed to connect to %s:%d\n", host.operator char *(), port);
        return 1;
    }
    connectHostDebugBridge();
    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        if (host_bridge_port <= 0) {
            stopHostDebugBridge();
            return 1;
        }
        printf("UI document not available; keeping Elara OS host alive for IDE debug bridge.\n");
        peer->close();
    }
    startExtLogicServer();
    launchPythonLogic();
    if (host_bridge_port > 0) {
        printf("Elara OS host running under IDE bridge. Waiting for IDE shutdown.\n");
        while (!quit_requested.load()) {
            if (!host_bridge_running.load()) {
                connectHostDebugBridge();
            }
            usleep(250000);
        }
        peer->close();
        stopHostDebugBridge();
        return 0;
    }
    printf("Commands: reload, snapshot, quit\n");
    char line[256];
    while (true) {
        printf("elara-os> ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        String command(line);
        command = command.trim();
        if (command == String("quit") || command == String("exit")) {
            break;
        }
        if (command == String("reload")) {
            buildDocument(ui);
            loadDocument(ui.toJson());
            continue;
        }
        if (command == String("snapshot")) {
            printSnapshot();
            continue;
        }
        printf("Unhandled command: %s\n", command.operator char *());
    }
    peer->close();
    stopHostDebugBridge();
    return 0;
}

}

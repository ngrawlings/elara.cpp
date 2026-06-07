#include "ElaraOsApp.h"

#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;

ElaraOsApp::ElaraOsApp(const String &value_host, int value_port, const String &value_host_bridge_host, int value_host_bridge_port)
    : host(value_host),
      port(value_port),
      host_bridge_host(value_host_bridge_host),
      host_bridge_port(value_host_bridge_port),
      host_bridge_fd(-1),
      virtual_drive_root("/tmp/elara-os-virtual-drives"),
      host_bridge_running(false),
      peer(new ElaraUiRpcPeer()) {
    const char *drive_root_env = getenv("ELARA_OS_VDRIVE_ROOT");
    if (drive_root_env && drive_root_env[0]) {
        virtual_drive_root = drive_root_env;
    }
}

ElaraOsApp::~ElaraOsApp() {
    stopHostDebugBridge();
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

void ElaraOsApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("ElaraOs"), 1080, 760, String("org.elara.ui.elara-os"));
    ui.setThemeMode(String("light"));
    ui.createGrid(String("app.shell"));
    ui.addGridColumnFill(String("app.shell"));
    ui.addGridRowFill(String("app.shell"));
    ui.setRootContent(String("app.shell"));
    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("elara.os.frame_authority"));
    ui.setPropertyString(String("app.surface"), String("backend_id"), String("org.elara.ui.elara-os.surface"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("Frame Authority"));
    ui.setSectionJson(
        String("app.surface"),
        String("commands"),
        String("["
               "{\"op\":\"clear\",\"r\":0.039,\"g\":0.055,\"b\":0.078},"
               "{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":720,\"r\":0.039,\"g\":0.055,\"b\":0.078},"
               "{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":116,\"r\":0.09,\"g\":0.118,\"b\":0.157},"
               "{\"op\":\"rect\",\"x\":104,\"y\":126,\"w\":1072,\"h\":468,\"r\":0.071,\"g\":0.09,\"b\":0.122},"
               "{\"op\":\"rect\",\"x\":120,\"y\":142,\"w\":1040,\"h\":436,\"r\":0.102,\"g\":0.133,\"b\":0.18},"
               "{\"op\":\"rect\",\"x\":120,\"y\":142,\"w\":1040,\"h\":12,\"r\":0.322,\"g\":0.573,\"b\":1.0},"
               "{\"op\":\"rect\",\"x\":168,\"y\":216,\"w\":92,\"h\":188,\"r\":0.322,\"g\":0.573,\"b\":1.0},"
               "{\"op\":\"rect\",\"x\":284,\"y\":216,\"w\":92,\"h\":248,\"r\":0.459,\"g\":0.878,\"b\":0.639},"
               "{\"op\":\"rect\",\"x\":400,\"y\":216,\"w\":92,\"h\":152,\"r\":1.0,\"g\":0.769,\"b\":0.361},"
               "{\"op\":\"rect\",\"x\":560,\"y\":224,\"w\":312,\"h\":28,\"r\":0.322,\"g\":0.573,\"b\":1.0},"
               "{\"op\":\"rect\",\"x\":560,\"y\":268,\"w\":244,\"h\":24,\"r\":0.459,\"g\":0.878,\"b\":0.639},"
               "{\"op\":\"rect\",\"x\":560,\"y\":308,\"w\":212,\"h\":24,\"r\":1.0,\"g\":0.769,\"b\":0.361},"
               "{\"op\":\"rect\",\"x\":560,\"y\":364,\"w\":468,\"h\":96,\"r\":0.122,\"g\":0.153,\"b\":0.204},"
               "{\"op\":\"rect\",\"x\":560,\"y\":476,\"w\":340,\"h\":18,\"r\":0.239,\"g\":0.278,\"b\":0.337},"
               "{\"op\":\"line\",\"x0\":88,\"y0\":610,\"x1\":1192,\"y1\":610,\"r\":0.275,\"g\":0.376,\"b\":0.51},"
               "{\"op\":\"rect\",\"x\":0,\"y\":664,\"w\":1280,\"h\":56,\"r\":0.055,\"g\":0.071,\"b\":0.094},"
               "{\"op\":\"rect\",\"x\":28,\"y\":682,\"w\":220,\"h\":12,\"r\":0.322,\"g\":0.573,\"b\":1.0},"
               "{\"op\":\"rect\",\"x\":1098,\"y\":682,\"w\":154,\"h\":12,\"r\":0.459,\"g\":0.878,\"b\":0.639},"
               "{\"op\":\"text\",\"x\":168,\"y\":176,\"text\":\"FRAME AUTHORITY\",\"size\":26,\"r\":0.92,\"g\":0.96,\"b\":1.0},"
               "{\"op\":\"text\",\"x\":560,\"y\":214,\"text\":\"Boot sequence\",\"size\":18,\"r\":0.88,\"g\":0.92,\"b\":0.98},"
               "{\"op\":\"text\",\"x\":560,\"y\":390,\"text\":\"IO chipset link established\",\"size\":16,\"r\":0.79,\"g\":0.84,\"b\":0.92},"
               "{\"op\":\"text\",\"x\":560,\"y\":420,\"text\":\"Awaiting window manager session\",\"size\":16,\"r\":0.79,\"g\":0.84,\"b\":0.92},"
               "{\"op\":\"text\",\"x\":24,\"y\":696,\"text\":\"Authority surface online\",\"size\":18,\"r\":0.86,\"g\":0.9,\"b\":0.95},"
               "{\"op\":\"text\",\"x\":1024,\"y\":696,\"text\":\"BOOT\",\"size\":18,\"r\":0.78,\"g\":0.95,\"b\":0.82}"
               "]")
    );
    ui.placeGridChild(String("app.shell"), String("app.surface"), 0, 0);
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
    const std::string drive0 = root + "/drive-1";
    const std::string drive1 = root + "/drive-2";
    const std::string fs_root = root + "/fs/root";

    if (!ensureDirectoryPath(root) ||
        !ensureDirectoryPath(drive0) ||
        !ensureDirectoryPath(drive1) ||
        !ensureDirectoryPath(fs_root)) {
        return false;
    }

    if (!ensureFileContents(
            drive0 + "/device.json",
            "{\n"
            "  \"drive_id\": 1,\n"
            "  \"block_size\": 4096,\n"
            "  \"block_count\": 16384,\n"
            "  \"flags\": 1,\n"
            "  \"mount_id\": 1,\n"
            "  \"mount_path\": \"/\",\n"
            "  \"role\": \"system\"\n"
            "}\n") ||
        !ensureFileContents(
            drive1 + "/device.json",
            "{\n"
            "  \"drive_id\": 2,\n"
            "  \"block_size\": 4096,\n"
            "  \"block_count\": 8192,\n"
            "  \"flags\": 0,\n"
            "  \"mount_id\": 2,\n"
            "  \"mount_path\": \"/data\",\n"
            "  \"role\": \"data\"\n"
            "}\n") ||
        !ensureFileContents(
            fs_root + "/README.txt",
            "Elara OS virtual root filesystem\n"
            "mount_id=1\n"
            "drive_id=1\n"
            "fs_kind=1\n") ||
        !ensureFileContents(
            root + "/manifest.json",
            "{\n"
            "  \"drives\": [1, 2],\n"
            "  \"mounts\": [\n"
            "    {\"mount_id\": 1, \"drive_id\": 1, \"path\": \"/\", \"flags\": 1},\n"
            "    {\"mount_id\": 2, \"drive_id\": 2, \"path\": \"/data\", \"flags\": 0}\n"
            "  ],\n"
            "  \"filesystem_authority\": \"elara.os.filesystem\",\n"
            "  \"block_io_authority\": \"elara.os.block_io\"\n"
            "}\n")) {
        return false;
    }

    return true;
}

bool ElaraOsApp::connectHostDebugBridge() {
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
             "\"status\":\"virtual drives ready\",\"root\":\"%s\",\"drives\":[1,2],\"block_io\":\"elara.os.block_io\",\"filesystem\":\"elara.os.filesystem\"",
             jsonEscape(virtual_drive_root.c_str()).c_str());
    sendHostDebugEvent("state", drive_payload);
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
                sendHostDebugEvent("pong", NULL);
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
    event += "\",\"session_id\":\"\"";
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
    if (!bootstrapVirtualDrives()) {
        printf("Failed to bootstrap virtual drives under %s\n", virtual_drive_root.c_str());
        return 1;
    }
    if (!peer->connect(host, (unsigned short)port)) {
        printf("Failed to connect to %s:%d\n", host.operator char *(), port);
        return 1;
    }
    connectHostDebugBridge();
    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        stopHostDebugBridge();
        return 1;
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

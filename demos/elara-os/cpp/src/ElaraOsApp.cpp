#include "ElaraOsApp.h"

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
      virtual_drive_root("/tmp/elara-os-virtual-drives"),
      owned_ui_server_pid(-1),
      owned_python_pid(-1),
      prefer_owned_ui_server(value_prefer_owned_ui_server),
      host_bridge_running(false),
      quit_requested(false),
      ext_logic_server_fd(-1),
      peer(new ElaraUiRpcPeer()) {
    const char *drive_root_env = getenv("ELARA_OS_VDRIVE_ROOT");
    if (drive_root_env && drive_root_env[0]) {
        virtual_drive_root = drive_root_env;
    }
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
    if (app && app->handleExtLogicRequest(method, params_json, result_json, error_code, error_message)) {
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
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("elara.os.frame_authority"));
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
               "{\"op\":\"text\",\"x\":462,\"y\":382,\"text\":\"waiting for EPA frame authority\",\"size\":18,\"r\":0.68,\"g\":0.78,\"b\":0.9}"
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
        printf("[C++ Host] connected to EPA debug VM at %s:%d\n", epa_dbg_host.operator char *(), epa_dbg_port);
    }
    return epa_dbg_fd >= 0;
}

bool ElaraOsApp::refreshDebugSessionConfigFromEnv() {
    const char *session_path = getenv("ELARA_DEBUG_SESSION");
    std::string text = readTextFile(session_path);
    if (text.empty()) {
        return false;
    }

    std::string session_epa_host = jsonStringField(text, "epa_dbg_host");
    int session_epa_port = jsonIntField(text, "epa_dbg_port", 0);
    std::string session_bundle_path = jsonStringField(text, "bundle_path");

    if (session_epa_host.size()) {
        epa_dbg_host = String(session_epa_host.c_str());
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
    if (!connectEpaDbg()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(epa_dbg_mutex);
    ByteArray request = BRpcRpcCodec::buildRequest(
        String("elara-os-host"),
        method,
        params_json.length() ? params_json : String("{}")
    );
    ByteArray frame = BRpcRpcCodec::framePayload(request);
    if (write(epa_dbg_fd, frame.operator const char *(), (size_t)frame.length()) != (ssize_t)frame.length()) {
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }

    unsigned char rhdr[4];
    if (!readExact(epa_dbg_fd, (char *)rhdr, 4)) {
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    uint32_t rlen = ((uint32_t)rhdr[0] << 24) | ((uint32_t)rhdr[1] << 16)
                  | ((uint32_t)rhdr[2] << 8)  | (uint32_t)rhdr[3];
    if (rlen > 4u * 1024u * 1024u) {
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    std::vector<char> response(rlen + 1, 0);
    if (!readExact(epa_dbg_fd, response.data(), rlen)) {
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
        close(epa_dbg_fd);
        epa_dbg_fd = -1;
        return false;
    }
    if (!ok) {
        String method_copy(method);
        printf("[C++ Host] EPA debug RPC failed method=%s code=%s msg=%s\n",
               method_copy.operator char *(),
               error_code.operator char *(),
               error_message.operator char *());
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
    if (!epaDbgCall(String("epa.debug.ingressPushHex"), ingress_params, ingress_result)) {
        error_message = String("epa.debug.ingressPushHex failed");
        return false;
    }

    sendHostDebugEvent(
        "ingress",
        "\"kernel\":\"elara.os.boot\",\"worker\":\"wid=1\",\"type\":\"BootDeviceList\",\"details\":\"hardware descriptor payload queued\""
    );

    String run_result;
    if (!epaDbgCall(
            String("epa.debug.run"),
            String("{\"path_id\":") + jsonQuoteString(path_id) + String(",\"max_ticks\":200000}"),
            run_result)) {
        error_message = String("epa.debug.run failed after boot ingress");
        return false;
    }
    result_json = String("{\"queued\":true,\"path_id\":\"boot\",\"payload_bytes\":")
        + String((int)(payload_hex.length() / 2))
        + String(",\"ingress\":") + (ingress_result.length() ? ingress_result : String("{}"))
        + String(",\"run\":") + (run_result.length() ? run_result : String("{}"))
        + String("}");
    sendHostDebugEvent("state", "\"status\":\"boot descriptor delivered to EPA\"");
    return true;
}

bool ElaraOsApp::handleExtLogicRequest(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("elara.os.bootDescriptor")) {
        String params_copy(params_json);
        std::string params(params_copy.operator char *() ? params_copy.operator char *() : "");
        String payload_hex(jsonStringField(params, "payload_hex").c_str());
        if (ingressBootDescriptor(payload_hex, result_json, error_message)) {
            return true;
        }
        error_code = String("boot_descriptor_failed");
        return false;
    }
    if (method == String("ext.debug.status")) {
        refreshDebugSessionConfigFromEnv();
        result_json = String("{\"epa_loaded\":") + String(epa_loaded ? "true" : "false")
            + String(",\"epa_dbg_port\":") + String(epa_dbg_port)
            + String(",\"ext_logic_server\":") + String(ext_logic_server_fd >= 0 ? "true" : "false")
            + String("}");
        return true;
    }
    error_code = String("not_found");
    error_message = String("unknown ext-logic method");
    return false;
}

void ElaraOsApp::startExtLogicServer() {
    if (ext_logic_server_fd >= 0 || host_bridge_port <= 0) {
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
        std::vector<char> frame;
        while (readBrpcFrame(client, frame)) {
            dispatchExtLogicFrame(this, client, frame);
        }
        close(client);
    }
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
    if (!bootstrapVirtualDrives()) {
        printf("Failed to bootstrap virtual drives under %s\n", virtual_drive_root.c_str());
        return 1;
    }
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

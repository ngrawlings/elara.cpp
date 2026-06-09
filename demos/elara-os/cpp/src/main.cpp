#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <unistd.h>
#include "ElaraOsApp.h"

using namespace elara;

namespace {

struct ElaraOsDebugSessionConfig {
    bool enabled;
    String session_path;
    String ui_rpc_host;
    int ui_rpc_port;
    String bundle_path;
    String epa_dbg_host;
    int epa_dbg_port;
    String host_debug_host;
    int host_debug_port;

    ElaraOsDebugSessionConfig()
        : enabled(false),
          session_path(""),
          ui_rpc_host(""),
          ui_rpc_port(0),
          bundle_path(""),
          epa_dbg_host(""),
          epa_dbg_port(0),
          host_debug_host(""),
          host_debug_port(0) {
    }
};

static std::string read_file_text(const char *path) {
    if (!path || !path[0]) return std::string();
    std::ifstream in(path);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string json_string_field(const std::string &json, const char *key) {
    std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) return m[1].str();
    return std::string();
}

static int json_int_field(const std::string &json, const char *key, int fallback) {
    std::regex re(std::string("\"") + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) return atoi(m[1].str().c_str());
    return fallback;
}

static ElaraOsDebugSessionConfig load_debug_session_from_env() {
    ElaraOsDebugSessionConfig cfg;
    const char *session_path = getenv("ELARA_DEBUG_SESSION");
    std::string text = read_file_text(session_path);
    if (text.empty()) {
        return cfg;
    }
    cfg.enabled = true;
    cfg.session_path = String(session_path ? session_path : "");
    cfg.ui_rpc_host = String(json_string_field(text, "ui_rpc_host").c_str());
    cfg.ui_rpc_port = json_int_field(text, "ui_rpc_port", 18820);
    cfg.bundle_path = String(json_string_field(text, "bundle_path").c_str());
    cfg.epa_dbg_host = String(json_string_field(text, "epa_dbg_host").c_str());
    cfg.epa_dbg_port = json_int_field(text, "epa_dbg_port", 0);
    cfg.host_debug_host = String(json_string_field(text, "host_debug_host").c_str());
    cfg.host_debug_port = json_int_field(text, "host_debug_port", 0);
    return cfg;
}

}

int main(int argc, const char *argv[]) {
    const char *stdout_fd_str = getenv("ELARA_STDOUT_FD");
    if (stdout_fd_str) {
        int fd = atoi(stdout_fd_str);
        if (fd > 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }

    ElaraOsDebugSessionConfig debug_session = load_debug_session_from_env();
    String host(debug_session.enabled && debug_session.ui_rpc_host.length()
        ? debug_session.ui_rpc_host
        : String("127.0.0.1"));
    int port = debug_session.enabled && debug_session.ui_rpc_port > 0
        ? debug_session.ui_rpc_port
        : 18820;
    String host_bridge_host(debug_session.enabled && debug_session.host_debug_host.length()
        ? debug_session.host_debug_host
        : String("127.0.0.1"));
    int host_bridge_port = debug_session.enabled ? debug_session.host_debug_port : 0;
    if (!debug_session.enabled) {
        if (argc > 1) host = String(argv[1]);
        if (argc > 2) port = atoi(argv[2]);
    }
    const char *bridge_host_env = getenv("ELARA_IDE_HOST_BRIDGE_HOST");
    const char *bridge_port_env = getenv("ELARA_IDE_HOST_BRIDGE_PORT");
    if (!debug_session.enabled) {
        if (bridge_host_env && bridge_host_env[0]) {
            host_bridge_host = String(bridge_host_env);
        }
        if (bridge_port_env && bridge_port_env[0]) {
            host_bridge_port = atoi(bridge_port_env);
        }
    }
    bool prefer_owned_ui_server = debug_session.enabled || argc <= 2;
    if (debug_session.enabled) {
        printf("Loaded IDE debug session: %s\n", debug_session.session_path.operator char *());
    }
    ElaraOsApp app(
        host,
        port,
        host_bridge_host,
        host_bridge_port,
        debug_session.epa_dbg_host,
        debug_session.epa_dbg_port,
        debug_session.bundle_path,
        prefer_owned_ui_server
    );
    return app.run();
}

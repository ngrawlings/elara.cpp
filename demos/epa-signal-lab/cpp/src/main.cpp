#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include "EpaSignalLabApp.h"

using namespace elara;

namespace {

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

static EpaSignalLabDebugSessionConfig load_debug_session_from_env() {
    EpaSignalLabDebugSessionConfig cfg;
    const char *session_path = getenv("ELARA_DEBUG_SESSION");
    std::string text = read_file_text(session_path);
    if (text.empty()) {
        return cfg;
    }
    cfg.enabled = true;
    cfg.session_path = String(session_path ? session_path : "");
    cfg.session_id = String(json_string_field(text, "session_id").c_str());
    cfg.ui_rpc_host = String(json_string_field(text, "ui_rpc_host").c_str());
    cfg.ui_rpc_port = json_int_field(text, "ui_rpc_port", 18777);
    cfg.bundle_path = String(json_string_field(text, "bundle_path").c_str());
    cfg.epa_dbg_host = String(json_string_field(text, "epa_dbg_host").c_str());
    cfg.epa_dbg_port = json_int_field(text, "epa_dbg_port", 0);
    cfg.host_debug_host = String(json_string_field(text, "host_debug_host").c_str());
    cfg.host_debug_port = json_int_field(text, "host_debug_port", 0);
    return cfg;
}

}

int main(int argc, const char *argv[]) {
    EpaSignalLabDebugSessionConfig debug_session = load_debug_session_from_env();
    String host(debug_session.enabled && debug_session.ui_rpc_host.length()
        ? debug_session.ui_rpc_host
        : String("127.0.0.1"));
    int port = debug_session.enabled && debug_session.ui_rpc_port > 0
        ? debug_session.ui_rpc_port
        : 18777;
    if (argc > 1) host = String(argv[1]);
    if (argc > 2) port = atoi(argv[2]);
    if (argc > 3) {
        debug_session.enabled = true;
        debug_session.host_debug_port = atoi(argv[3]);
        debug_session.host_debug_host = argc > 4 ? String(argv[4]) : String("127.0.0.1");
    }
    if (debug_session.enabled) {
        printf("Loaded IDE debug session: %s\n", debug_session.session_path.operator char *());
    }
    EpaSignalLabApp app(host, port, debug_session);
    return app.run();
}

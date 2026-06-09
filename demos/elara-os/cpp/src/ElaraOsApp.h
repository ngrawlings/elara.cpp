#ifndef ELARAOSAPP_H
#define ELARAOSAPP_H

#include <atomic>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

class ElaraOsApp {
public:
    ElaraOsApp(
        const String &host,
        int port,
        const String &host_bridge_host,
        int host_bridge_port,
        const String &epa_dbg_host,
        int epa_dbg_port,
        const String &bundle_path,
        bool prefer_owned_ui_server = false
    );
    ~ElaraOsApp();
    int run();
    bool handleExtLogicRequest(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);

private:
    String host;
    int port;
    String host_bridge_host;
    int host_bridge_port;
    int host_bridge_fd;
    String epa_dbg_host;
    int epa_dbg_port;
    String bundle_path;
    int epa_dbg_fd;
    bool epa_loaded;
    bool boot_payload_pending;
    String pending_boot_payload_hex;
    std::string virtual_drive_root;
    pid_t owned_ui_server_pid;
    pid_t owned_python_pid;
    bool prefer_owned_ui_server;
    std::atomic<bool> host_bridge_running;
    std::atomic<bool> quit_requested;
    std::thread host_bridge_thread;
    std::thread ext_logic_thread;
    std::mutex ext_logic_request_mutex;
    std::mutex host_bridge_mutex;
    std::mutex epa_dbg_mutex;
    int ext_logic_server_fd;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool printSnapshot();
    bool connectUiPeer();
    bool launchUiServerFallback();
    int chooseUiFallbackPort() const;
    void recordLaunchedPid(const char *label, pid_t pid) const;
    void stopOwnedUiServer();
    bool launchPythonLogic();
    void stopOwnedPythonLogic();
    bool bootstrapVirtualDrives();
    bool connectEpaDbg();
    void closeEpaDbg();
    bool refreshDebugSessionConfigFromEnv();
    bool epaDbgCall(const String &method, const String &params_json, String &result_json);
    bool epaDbgLoadBundle();
    bool ingressBootDescriptor(const String &payload_hex, String &result_json, String &error_message);
    bool continueBootDescriptor(String &result_json, String &error_message);
    void startExtLogicServer();
    void extLogicServe();
    bool ensureDirectoryPath(const std::string &path);
    bool ensureFileContents(const std::string &path, const std::string &contents);
    bool connectHostDebugBridge();
    void stopHostDebugBridge();
    void hostDebugBridgeLoop();
    bool sendHostDebugEvent(const char *kind, const char *payload);
};

}

#endif

#ifndef EPASIGNALLABAPP_H
#define EPASIGNALLABAPP_H

#include <mutex>
#include <thread>
#include <vector>

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>

#include "EpaSignalLabEpaVmHost.h"

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

struct EpaSignalLabDebugSessionConfig {
    bool enabled;
    String session_path;
    String session_id;
    String ui_rpc_host;
    int ui_rpc_port;
    String bundle_path;
    String epa_dbg_host;
    int epa_dbg_port;
    String host_debug_host;
    int host_debug_port;

    EpaSignalLabDebugSessionConfig()
        : enabled(false),
          ui_rpc_port(18777),
          epa_dbg_port(0),
          host_debug_port(0) {
    }
};

class EpaSignalLabApp {
public:
    EpaSignalLabApp(const String &host, int port, const EpaSignalLabDebugSessionConfig &debug_session = EpaSignalLabDebugSessionConfig());
    int run();
    void handleUiAction(const String &target, const String &action);
    void handleKernelSignal(uint8_t wid, const char *msg, int msg_len);

private:
    String host;
    int port;
    String bundle_path;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;
    EpaSignalLabEpaVmHost epa;
    std::mutex state_mutex;
    std::vector<String> log_lines;
    String status_text;
    bool ui_dirty;
    bool should_quit;
    bool epa_loaded;
    bool epa_started;
    uint32_t next_seq;
    EpaSignalLabDebugSessionConfig debug_session;
    int host_debug_fd;
    std::mutex host_debug_io_mutex;
    int ext_logic_server_fd;
    std::thread ext_logic_thread;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool pushUiState();
    void enableUiEvents();
    void appendLog(const String &line);
    void setStatus(const String &line);
    void clearLog();
    bool refreshEpaState();
    void installEntrySignalCallback();
    bool queueSampleIngress(int left_value, int right_value);
    void appendKernelStatusSummary();
    bool printSnapshot();
    void sendHostDebugEvent(const String &kind, const String &payload_json);
    void sendHostDebugLog(const String &message);
    void sendHostDebugState(const String &status);
    bool connectHostDebugBridge();
    void closeHostDebugBridge();
    void startHostDebugReader();
    void hostDebugReadLoop();
    void startExtLogicServer();
    void extLogicServe();
};

}

#endif

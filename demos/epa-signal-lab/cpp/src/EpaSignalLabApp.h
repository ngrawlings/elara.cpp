#ifndef EPASIGNALLABAPP_H
#define EPASIGNALLABAPP_H

#include <mutex>
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

class EpaSignalLabApp {
public:
    EpaSignalLabApp(const String &host, int port);
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
};

}

#endif

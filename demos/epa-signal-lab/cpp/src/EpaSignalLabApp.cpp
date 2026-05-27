#include "EpaSignalLabApp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <libelaraformat/json/types/JsonString.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;
using sockets::rpc::json::JsonRPCCodec;

namespace {

static EpaSignalLabApp *g_epa_signal_lab_app = NULL;

static uint32_t read_le_u32(const unsigned char *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int on_entry_kernel_signal(uint8_t wid, const char *msg, const int msg_len) {
    if (g_epa_signal_lab_app) {
        g_epa_signal_lab_app->handleKernelSignal(wid, msg, msg_len);
    }
    return 1;
}

class UiEventSinkService : public sockets::rpc::json::JsonRPCService {
public:
    explicit UiEventSinkService(EpaSignalLabApp *value_app)
        : sockets::rpc::json::JsonRPCService("ui"),
          app(value_app) {
    }

    bool call(
        const String &method,
        const String &params_json,
        String &result_json,
        String &error_code,
        String &error_message
    ) {
        if (method == String("event")) {
            String target;
            String action;
            JsonRPCCodec::getStringField(params_json, String("target"), target);
            JsonRPCCodec::getStringField(params_json, String("action"), action);
            if (app) {
                app->handleUiAction(target, action);
            }
            result_json = String("{\"received\":true}");
            return true;
        }

        error_code = String("method_not_found");
        error_message = String("No client-side ui event handler matched the request");
        return false;
    }

private:
    EpaSignalLabApp *app;
};

}

EpaSignalLabApp::EpaSignalLabApp(const String &value_host, int value_port)
    : host(value_host),
      port(value_port),
      bundle_path(String("..") + String("/") + String("build") + String("/") + String("epa.bin")),
      peer(new ElaraUiRpcPeer()),
      ui_dirty(true),
      should_quit(false),
      epa_loaded(false),
      epa_started(false),
      next_seq(1u) {
}

void EpaSignalLabApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("EpaSignalLab"), 1040, 760, String("org.elara.ui.epa-signal-lab"));
    ui.setThemeMode(String("light"));
    ui.createGrid(String("app.panel"));
    ui.setRootContent(String("app.panel"));

    ui.addGridColumnExact(String("app.panel"), 20);
    ui.addGridColumnFill(String("app.panel"));
    ui.addGridColumnExact(String("app.panel"), 180);
    ui.addGridColumnExact(String("app.panel"), 180);
    ui.addGridColumnExact(String("app.panel"), 180);
    ui.addGridColumnExact(String("app.panel"), 180);
    ui.addGridColumnExact(String("app.panel"), 20);

    ui.addGridRowExact(String("app.panel"), 20);
    ui.addGridRowExact(String("app.panel"), 34);
    ui.addGridRowExact(String("app.panel"), 52);
    ui.addGridRowExact(String("app.panel"), 48);
    ui.addGridRowExact(String("app.panel"), 48);
    ui.addGridRowFill(String("app.panel"));
    ui.addGridRowExact(String("app.panel"), 20);

    ui.createLabel(String("app.title"), String("EPA Communication Lab"), 22);
    ui.createLabel(
        String("app.status"),
        String("Status: waiting for bundle load"),
        14
    );
    ui.createLabel(
        String("app.help"),
        String("Demonstrates local GHS handoff, signal() to kernel, far_signal() to a remote kernel, and frame_commit() host delivery."),
        13
    );
    ui.createButton(String("app.reload"), String("Reload Bundle"), String("app.reload"));
    ui.createButton(String("app.queue_a"), String("Queue Sample A"), String("app.queue_a"));
    ui.createButton(String("app.queue_b"), String("Queue Sample B"), String("app.queue_b"));
    ui.createButton(String("app.snapshot"), String("Snapshot"), String("app.snapshot"));
    ui.createButton(String("app.clear"), String("Clear Log"), String("app.clear"));
    ui.createListView(String("app.log"));
    ui.setPropertyNumber(String("app.log"), String("font_size"), 13);
    ui.setSectionJson(String("app.log"), String("items"), String("[]"));

    ui.placeGridChild(String("app.panel"), String("app.title"), 1, 1, 5, 1);
    ui.placeGridChild(String("app.panel"), String("app.status"), 1, 2, 5, 1);
    ui.placeGridChild(String("app.panel"), String("app.help"), 1, 3, 5, 1);
    ui.placeGridChild(String("app.panel"), String("app.reload"), 2, 4);
    ui.placeGridChild(String("app.panel"), String("app.queue_a"), 3, 4);
    ui.placeGridChild(String("app.panel"), String("app.queue_b"), 4, 4);
    ui.placeGridChild(String("app.panel"), String("app.snapshot"), 5, 4);
    ui.placeGridChild(String("app.panel"), String("app.clear"), 5, 2);
    ui.placeGridChild(String("app.panel"), String("app.log"), 1, 5, 5, 1);
}

bool EpaSignalLabApp::loadDocument(const String &document_json) {
    String params = String("{\"document\":") + JsonString(document_json, true).toString() + String("}");
    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(String("ui.loadDocument"), params, result_json, error_code, error_message, 5000)) {
        printf("ui.loadDocument failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        return false;
    }
    return true;
}

void EpaSignalLabApp::enableUiEvents() {
    String result_json;
    String error_code;
    String error_message;
    peer->call(String("ui.enableEvent"), String("{\"action\":\"clicked\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"action\"}"), result_json, error_code, error_message, 5000);
}

void EpaSignalLabApp::appendLog(const String &line) {
    std::lock_guard<std::mutex> lock(state_mutex);
    log_lines.push_back(line);
    while (log_lines.size() > 48u) {
        log_lines.erase(log_lines.begin());
    }
    ui_dirty = true;
}

void EpaSignalLabApp::setStatus(const String &line) {
    std::lock_guard<std::mutex> lock(state_mutex);
    status_text = line;
    ui_dirty = true;
}

void EpaSignalLabApp::clearLog() {
    std::lock_guard<std::mutex> lock(state_mutex);
    log_lines.clear();
    ui_dirty = true;
}

bool EpaSignalLabApp::pushUiState() {
    std::vector<String> lines_copy;
    String status_copy;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!ui_dirty) {
            return true;
        }
        lines_copy = log_lines;
        status_copy = status_text;
        ui_dirty = false;
    }

    String items_json("[");
    for (size_t i = 0; i < lines_copy.size(); i++) {
        if (i > 0u) {
            items_json += String(",");
        }
        items_json += String("{\"id\":\"row");
        items_json += String((int)i);
        items_json += String("\",\"label\":");
        items_json += JsonString(lines_copy[i], true).toString();
        items_json += String("}");
    }
    items_json += String("]");

    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(
            String("ui.setText"),
            String("{\"target\":\"app.status\",\"value\":") + JsonString(status_copy, true).toString() + String("}"),
            result_json,
            error_code,
            error_message,
            5000)) {
        printf("ui.setText failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        return false;
    }
    if (!peer->call(
            String("ui.setSectionJson"),
            String("{\"target\":\"app.log\",\"section\":\"items\",\"value\":") + JsonString(items_json, true).toString() + String("}"),
            result_json,
            error_code,
            error_message,
            5000)) {
        printf("ui.setSectionJson failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        return false;
    }
    return true;
}

void EpaSignalLabApp::installEntrySignalCallback() {
    int idx = epa.findKernelIndex(String("entry"));
    if (idx < 0) {
        appendLog(String("entry kernel not found for callback installation"));
        return;
    }
    EpaKernel *kernel = epa.rawKernelAt((size_t)idx);
    if (!kernel) {
        appendLog(String("entry kernel callback install failed: null kernel"));
        return;
    }
    epa_kernel_set_signal_callback(kernel, on_entry_kernel_signal);
    appendLog(String("installed signal callback on kernel 'entry'"));
}

bool EpaSignalLabApp::refreshEpaState() {
    epa_loaded = false;
    epa_started = false;
    epa.destroy();
    if (!epa.loadBundlePath(bundle_path)) {
        setStatus(String("Status: bundle load failed"));
        appendLog(String("bundle load failed: ") + epa.lastError());
        return false;
    }
    epa_loaded = true;
    installEntrySignalCallback();
    if (!epa.startAllKernels()) {
        setStatus(String("Status: kernel start failed"));
        appendLog(String("start_all_kernels failed: ") + epa.lastError());
        return false;
    }
    epa_started = true;
    setStatus(String("Status: bundle loaded and kernels running"));
    appendKernelStatusSummary();
    return true;
}

bool EpaSignalLabApp::queueSampleIngress(int left_value, int right_value) {
    struct DemoIngressPayload {
        uint32_t seq;
        uint32_t left_value;
        uint32_t right_value;
    } payload;
    int idx;

    if (!epa_started) {
        appendLog(String("queue failed: kernels not started"));
        return false;
    }
    idx = epa.findKernelIndex(String("entry"));
    if (idx < 0) {
        appendLog(String("queue failed: entry kernel not found"));
        return false;
    }

    payload.seq = next_seq++;
    payload.left_value = (uint32_t)left_value;
    payload.right_value = (uint32_t)right_value;
    if (!epa.ingressPushToKernel((size_t)idx, 1u, &payload, sizeof(payload))) {
        appendLog(String("queue failed: ") + epa.lastError());
        return false;
    }
    appendLog(String("queued ingress seq=") + String((int)payload.seq)
        + String(" left=") + String((int)payload.left_value)
        + String(" right=") + String((int)payload.right_value));
    return true;
}

void EpaSignalLabApp::appendKernelStatusSummary() {
    size_t count = epa.kernelCount();
    for (size_t i = 0; i < count; i++) {
        appendLog(String("kernel[") + String((int)i)
            + String("] id=") + epa.kernelPathId(i)
            + String(" status=") + epa.kernelStatusText(i)
            + String(" threads=") + String((int)epa.kernelThreadCount(i)));
    }
}

bool EpaSignalLabApp::printSnapshot() {
    String result_json;
    String error_code;
    String error_message;
    if (peer->call(String("ui.snapshot"), String("{}"), result_json, error_code, error_message, 5000)) {
        printf("%s\n", result_json.operator char *());
        appendLog(String("snapshot printed to stdout"));
        return true;
    }
    appendLog(String("ui.snapshot failed: ") + error_code + String(" ") + error_message);
    return false;
}

void EpaSignalLabApp::handleUiAction(const String &target, const String &action) {
    if (!(action == String("clicked") || action == String("action"))) {
        return;
    }
    if (target == String("app.reload")) {
        appendLog(String("ui action: reload bundle"));
        refreshEpaState();
        return;
    }
    if (target == String("app.queue_a")) {
        queueSampleIngress(4, 9);
        return;
    }
    if (target == String("app.queue_b")) {
        queueSampleIngress(12, 33);
        return;
    }
    if (target == String("app.snapshot")) {
        printSnapshot();
        return;
    }
    if (target == String("app.clear")) {
        clearLog();
        appendLog(String("log cleared"));
        return;
    }
}

void EpaSignalLabApp::handleKernelSignal(uint8_t wid, const char *msg, int msg_len) {
    const unsigned char *bytes = (const unsigned char*)msg;
    if (!msg || msg_len < 4) {
        appendLog(String("signal callback wid=") + String((int)wid) + String(" mailbox=empty"));
        return;
    }
    if (msg_len >= 28 && read_le_u32(bytes) == 0x45465231u) {
        uint32_t version = read_le_u32(bytes + 4);
        uint32_t seq = read_le_u32(bytes + 8);
        uint32_t left_value = read_le_u32(bytes + 12);
        uint32_t right_value = read_le_u32(bytes + 16);
        uint32_t sum = read_le_u32(bytes + 20);
        uint32_t route = read_le_u32(bytes + 24);
        appendLog(String("host frame wid=") + String((int)wid)
            + String(" v=") + String((int)version)
            + String(" seq=") + String((int)seq)
            + String(" left=") + String((int)left_value)
            + String(" right=") + String((int)right_value)
            + String(" sum=") + String((int)sum)
            + String(" route=") + String((int)route));
        return;
    }
    appendLog(String("signal wid=") + String((int)wid)
        + String(" mailbox0=") + String((int)read_le_u32(bytes))
        + String(" len=") + String(msg_len));
}

int EpaSignalLabApp::run() {
    g_epa_signal_lab_app = this;
    peer->addService(Ref<sockets::rpc::json::JsonRPCService>(new UiEventSinkService(this)));

    if (!peer->connect(host, (unsigned short)port)) {
        printf("Failed to connect to %s:%d\n", host.operator char *(), port);
        g_epa_signal_lab_app = NULL;
        return 1;
    }

    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        peer->close();
        g_epa_signal_lab_app = NULL;
        return 1;
    }
    enableUiEvents();
    appendLog(String("connected to UI RPC head at ") + host + String(":") + String(port));
    appendLog(String("bundle path: ") + bundle_path);
    refreshEpaState();
    pushUiState();

    while (!should_quit) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int rc = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (rc > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[256];
            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }
            String command(line);
            command = command.trim();
            if (command == String("quit") || command == String("exit")) {
                break;
            } else if (command == String("reload")) {
                refreshEpaState();
            } else if (command == String("queue")) {
                queueSampleIngress(4, 9);
            } else if (command == String("queue2")) {
                queueSampleIngress(12, 33);
            } else if (command == String("clear")) {
                clearLog();
                appendLog(String("log cleared"));
            } else if (command == String("snapshot")) {
                printSnapshot();
            } else {
                appendLog(String("unhandled command: ") + command);
            }
        }
        if (!pushUiState()) {
            break;
        }
    }

    epa.stopAllKernels();
    epa.destroy();
    peer->close();
    g_epa_signal_lab_app = NULL;
    return 0;
}

}

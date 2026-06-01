#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <libelaraformat/json/types/JsonString.h>
#include <libelaraformat/json/types/JsonValue.h>

#include <libelaravector/elara_vector.h>
#include <libelaravectorcpp/ElaraVectorDocument.h>

#include <libelaraui/config.h>
#include <libelaraui/ElaraGui.h>
#include <libelaraui/ElaraJsonUiProtocol.h>
#include <libelaraui/backends/gtk/GtkGuiBackend.h>
#include <libelaraui/frontend/ElaraEventResponder.h>
#include <libelaraui/frontend/listeners/WidgetListener.h>
#include <libelaraui/frontend/theme/ElaraTheme.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraRootWidget.h>
#include <libelaraui/frontend/widgets/ElaraTextInputWidget.h>

#include <libelarauirpc/ElaraUiRpcPeer.h>
#include <libelarauirpc/ElaraUiRpcUiBridge.h>
#include <libelarauirpc/ElaraUiRpcUiService.h>

#include <libelarathreads/Mutex.h>

using namespace elara;
using namespace elara::ui::rpc;

namespace {

static bool jsonBoolValue(const Json& json, const String& path, bool fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value || value->getType() == JsonValue::INVALID) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text == String("true") || text == String("\"true\"")) {
        return true;
    }

    if(text == String("false") || text == String("\"false\"")) {
        return false;
    }

    return fallback;
}

static EvDocument *buildDemoOverlay() {
    const float w = 200.0f;
    const float h = 72.0f;

    EvDocument *doc = ev_document_create(w, h);
    if (!doc) return 0;

    EvNode *bg = ev_rect(0, 0, w, h);
    ev_set_fill(bg, ev_rgba(255, 248, 180, 220));
    ev_set_stroke(bg, ev_rgba(140, 110, 0, 255), 2.0f);
    ev_document_add_child(doc, bg);

    EvNode *dot = ev_circle(w - 16.0f, 16.0f, 8.0f);
    ev_set_fill(dot, ev_rgba(60, 200, 60, 220));
    ev_document_add_child(doc, dot);

    EvNode *title = ev_node_create(EV_NODE_TEXT);
    if (title) {
        title->data.text.x = 10.0f;
        title->data.text.y = 28.0f;
        title->data.text.text = strdup("Vector Overlay");
        title->data.text.size = 13.0f;
        ev_set_fill(title, ev_rgba(80, 50, 0, 255));
        ev_document_add_child(doc, title);
    }

    EvNode *sub = ev_node_create(EV_NODE_TEXT);
    if (sub) {
        sub->data.text.x = 10.0f;
        sub->data.text.y = 50.0f;
        sub->data.text.text = strdup("debug.demo overlay");
        sub->data.text.size = 10.0f;
        ev_set_fill(sub, ev_rgba(100, 80, 20, 200));
        ev_document_add_child(doc, sub);
    }

    return doc;
}

class DeferredUiRequest {
public:
    String method;
    String params_json;
    String result_json;
    String error_code;
    String error_message;
    bool completed;
    bool ok;
    Mutex mutex;

    DeferredUiRequest(const String& request_method, const String& request_params)
        : method(request_method),
          params_json(request_params),
          completed(false),
          ok(false),
          mutex("deferred-ui-request") {
    }
};

class EventArtifactLogger {
private:
    String session_dir;
    String event_log_path;
    String manifest_path;
    String summary_path;
    Mutex mutex;
    FILE* event_log;
    int event_count;

    String timestampNow() const {
        char buffer[64];
        time_t now = time(0);
        struct tm local_tm;
        localtime_r(&now, &local_tm);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_tm);
        return String(buffer);
    }

    String sessionStamp() const {
        char buffer[64];
        time_t now = time(0);
        struct tm local_tm;
        localtime_r(&now, &local_tm);
        strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local_tm);
        return String(buffer);
    }

    void ensureDir(const String& path) {
        mkdir((const char*)path, 0775);
    }

    void writeFile(const String& path, const String& content) {
        FILE* file = fopen((const char*)path, "w");
        if(!file) {
            return;
        }

        fwrite((const char*)content, 1, content.byteLength(), file);
        fclose(file);
    }

public:
    EventArtifactLogger()
        : mutex("event-artifact-logger"),
          event_log(0),
          event_count(0) {
        ensureDir("artifacts");

        session_dir = String("artifacts/") + sessionStamp() + String("-ui-rpc-debug");
        ensureDir(session_dir);

        event_log_path = session_dir + String("/event.log");
        manifest_path = session_dir + String("/manifest.txt");
        summary_path = session_dir + String("/summary.txt");

        event_log = fopen((const char*)event_log_path, "a");

        writeFile(
            manifest_path,
            String("kind=ui-rpc-debug\n") +
            String("created=") + timestampNow() + String("\n") +
            String("event_log=") + event_log_path + String("\n")
        );

        writeFile(
            String("artifacts/latest.txt"),
            session_dir + String("\n")
        );

        writeSummary();
    }

    ~EventArtifactLogger() {
        Mutex::Lock lock(mutex);

        if(event_log) {
            fclose(event_log);
            event_log = 0;
        }
    }

    String getSessionDir() const {
        return session_dir;
    }

    void writeSummary() {
        writeFile(
            summary_path,
            String("session=") + session_dir + String("\n") +
            String("events=") + String(event_count) + String("\n")
        );
    }

    void log(const String& category, const String& message) {
        Mutex::Lock lock(mutex);

        if(!event_log) {
            return;
        }

        String line = timestampNow() + String(" [") + category + String("] ") + message + String("\n");
        printf("%s\n", (const char*)line);
        fwrite((const char*)line, 1, line.byteLength(), event_log);
        fflush(event_log);
        event_count++;
        writeSummary();
    }
};

class DebugEventLog : public ElaraUiRpcEventSink {
private:
    FILE* file;
    Mutex mutex;
    int seq;

    long long nowMs() const {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
    }

    void writeLine(const String& line) {
        if(!file) {
            return;
        }
        fwrite((const char*)line, 1, line.byteLength(), file);
        fwrite("\n", 1, 1, file);
        fflush(file);
    }

public:
    DebugEventLog(const char* path)
        : mutex("debug-event-log"),
          seq(0) {
        file = fopen(path, "a");
        if(file) {
            fprintf(file, "{\"type\":\"session_start\",\"ts_ms\":%lld}\n", nowMs());
            fflush(file);
        }
    }

    ~DebugEventLog() {
        if(file) {
            fclose(file);
            file = 0;
        }
    }

    bool isOpen() const {
        return file != 0;
    }

    void logRpcIn(const String& method, const String& params_json) {
        Mutex::Lock lock(mutex);
        int s = seq++;
        long long ts = nowMs();
        String line =
            String("{\"type\":\"rpc_in\",\"seq\":") + String(s) +
            String(",\"ts_ms\":") + String((int)ts) +
            String(",\"method\":\"") + method +
            String("\",\"params\":") + params_json +
            String("}");
        writeLine(line);
    }

    void logRpcOut(const String& method, bool ok, const String& error_code) {
        Mutex::Lock lock(mutex);
        int s = seq++;
        long long ts = nowMs();
        String line =
            String("{\"type\":\"rpc_out\",\"seq\":") + String(s) +
            String(",\"ts_ms\":") + String((int)ts) +
            String(",\"method\":\"") + method +
            String("\",\"ok\":") + String(ok ? "true" : "false");
        if(error_code.length() > 0) {
            line = line + String(",\"error\":\"") + error_code + String("\"");
        }
        line = line + String("}");
        writeLine(line);
    }

    void onOutboundEvent(
        const String& target,
        const String& action,
        const String& payload,
        int event_seq,
        long long ts_ms
    ) {
        Mutex::Lock lock(mutex);
        int s = seq++;
        String line =
            String("{\"type\":\"event_out\",\"seq\":") + String(s) +
            String(",\"event_seq\":") + String(event_seq) +
            String(",\"ts_ms\":") + String((int)ts_ms) +
            String(",\"target\":\"") + target +
            String("\",\"action\":\"") + action +
            String("\",\"payload\":") + payload +
            String("}");
        writeLine(line);
    }
};

class EventTraceListener : public WidgetListener {
private:
    EventArtifactLogger* logger;

    String handleToString(ElaraWidgetHandle handle) const {
        Memory memory = handle.getHandle();
        return String((const char*)memory.getPtr(), memory.length());
    }

public:
    EventTraceListener(EventArtifactLogger* event_logger)
        : logger(event_logger) {
    }

    void onWidgetMouseMove(ElaraWidgetHandle handle, double x, double y) {
        if(logger) {
            logger->log("widget.mouseMove", handleToString(handle) + String(" x=") + String(x) + String(" y=") + String(y));
        }
    }

    void onWidgetMouseDown(ElaraWidgetHandle handle, int button, double x, double y) {
        if(logger) {
            logger->log("widget.mouseDown", handleToString(handle) + String(" button=") + String(button) + String(" x=") + String(x) + String(" y=") + String(y));
        }
    }

    void onWidgetMouseUp(ElaraWidgetHandle handle, int button, double x, double y) {
        if(logger) {
            logger->log("widget.mouseUp", handleToString(handle) + String(" button=") + String(button) + String(" x=") + String(x) + String(" y=") + String(y));
        }
    }

    void onWidgetClicked(ElaraWidgetHandle handle, int button, double x, double y) {
        if(logger) {
            logger->log("widget.clicked", handleToString(handle) + String(" button=") + String(button) + String(" x=") + String(x) + String(" y=") + String(y));
        }
    }

    void onWidgetHoverChanged(ElaraWidgetHandle handle, bool hovered) {
        if(logger) {
            logger->log("widget.hover", handleToString(handle) + String(" hovered=") + String(hovered ? "true" : "false"));
        }
    }

    void onWidgetKeyDown(ElaraWidgetHandle handle, unsigned int keyval) {
        if(logger) {
            logger->log("widget.keyDown", handleToString(handle) + String(" keyval=") + String((int)keyval));
        }
    }

    void onWidgetKeyUp(ElaraWidgetHandle handle, unsigned int keyval) {
        if(logger) {
            logger->log("widget.keyUp", handleToString(handle) + String(" keyval=") + String((int)keyval));
        }
    }

    void onWidgetKeysTyped(ElaraWidgetHandle handle, const String& text) {
        if(logger) {
            logger->log("widget.keysTyped", handleToString(handle) + String(" text=") + text);
        }
    }
};

namespace {

String targetWindowId(const String& target) {
    String copy(target);
    int separator = copy.indexOf(String("::"));

    if(separator <= 0) {
        return String();
    }

    return copy.substr(0, separator).trim();
}

bool targetOwnedByRoot(ElaraRootWidget* root, const String& target) {
    if(!root) {
        return false;
    }

    String target_window_id = targetWindowId(target);

    if(target_window_id.length() > 0 && root->getRootId() != target_window_id) {
        return false;
    }

    return root->getWidget(ElaraWidgetHandle(target)).getPtr() != 0;
}

bool targetsSpecificWidget(const String& method) {
    return method == String("scrollToBottom") ||
        method == String("setText") ||
        method == String("setVisible") ||
        method == String("setEnabled") ||
        method == String("setChecked") ||
        method == String("setBounds") ||
        method == String("setFocus") ||
        method == String("clearChildren") ||
        method == String("replaceChildren") ||
        method == String("clickWidget") ||
        method == String("typeWidget") ||
        method == String("performAction") ||
        method == String("snapshotWidget");
}

}

class SecondaryWindowManager {
private:
    class WindowRecord {
    public:
        String window_id;
        Ref<ElaraDrawSurface> surface;
        ElaraRootWidget* root;
        ElaraJsonUiProtocol* protocol;

        WindowRecord()
            : root(0),
              protocol(0) {
        }

        ~WindowRecord() {
            if(protocol) {
                delete protocol;
                protocol = 0;
            }
            root = 0;
            surface = Ref<ElaraDrawSurface>(0);
        }
    };

    Ref<ElaraGuiBackend> backend;
    ElaraTheme* theme;
    EventArtifactLogger* logger;
    Array<WindowRecord*> windows;

    WindowRecord* findWindow(const String& window_id) const {
        for(int i = 0; i < (int)windows.length(); i++) {
            WindowRecord* record = windows[i];
            if(record && record->window_id == window_id) {
                return record;
            }
        }
        return 0;
    }

    void enableDefaultOutboundEvents(ElaraRootWidget* root) {
        if(!root) {
            return;
        }

        root->enableOutboundEvent("mouseMove");
        root->enableOutboundEvent("mouseDown");
        root->enableOutboundEvent("mouseUp");
        root->enableOutboundEvent("clicked");
        root->enableOutboundEvent("hoverChanged");
        root->enableOutboundEvent("keyDown");
        root->enableOutboundEvent("keyUp");
        root->enableOutboundEvent("keysTyped");
        root->enableOutboundEvent("valueChanged");
        root->enableOutboundEvent("action");
    }

public:
    SecondaryWindowManager(
        Ref<ElaraGuiBackend> gui_backend,
        ElaraTheme* ui_theme,
        EventArtifactLogger* event_logger
    )
        : backend(gui_backend),
          theme(ui_theme),
          logger(event_logger) {
    }

    ~SecondaryWindowManager() {
        while(windows.length() > 0) {
            WindowRecord* record = windows[0];
            windows.remove(0);
            delete record;
        }
    }

    bool openWindow(
        const String& window_id,
        const String& title,
        int width,
        int height,
        const String& document,
        String& error_message
    ) {
        if(window_id.length() <= 0) {
            error_message = "window_id is required";
            return false;
        }

        closeWindow(window_id);

        WindowRecord* record = new WindowRecord();
        record->window_id = window_id;
        record->surface = Ref<ElaraDrawSurface>(new ElaraRootWidget(window_id));
        record->root = dynamic_cast<ElaraRootWidget*>(record->surface.getPtr());
        if(record->root) {
            record->root->setGuiBackend(backend.getPtr());
        }
        record->protocol = new ElaraJsonUiProtocol(record->root, theme);

        if(!record->root || !record->protocol || !record->protocol->load(document)) {
            error_message = "The window document could not be loaded";
            delete record;
            return false;
        }

        enableDefaultOutboundEvents(record->root);
        backend->createWindow(title, width, height, record->surface);
        backend->showSurface(record->surface);
        backend->invalidate();
        windows.push(record);

        if(logger) {
            logger->log("ui.window", String("opened id=") + window_id + String(" title=") + title);
        }

        return true;
    }

    bool closeWindow(const String& window_id) {
        for(int i = 0; i < (int)windows.length(); i++) {
            WindowRecord* record = windows[i];
            if(!record || !(record->window_id == window_id)) {
                continue;
            }

            GtkGuiBackend* gtk_backend = dynamic_cast<GtkGuiBackend*>(backend.getPtr());
            if(gtk_backend) {
                gtk_backend->destroyWindow(record->surface);
            } else if(record->root) {
                record->root->sweepRegistry();
            }

            windows.remove(i);
            delete record;
            backend->invalidate();

            if(logger) {
                logger->log("ui.window", String("closed id=") + window_id);
            }

            return true;
        }

        return false;
    }

    void setMainWindowTitle(const String& title) {
        GtkGuiBackend* gtk_backend = dynamic_cast<GtkGuiBackend*>(backend.getPtr());
        if(gtk_backend) {
            gtk_backend->setWindowTitle(title);
        }
    }

    bool hasWindow(const String& window_id) const {
        return findWindow(window_id) != 0;
    }

    String snapshotAll() const {
        String result = "[";
        for(int i = 0; i < (int)windows.length(); i++) {
            WindowRecord* record = windows[i];
            if(!record || !record->root) {
                continue;
            }
            if(result.length() > 1) {
                result = result + String(",");
            }
            result = result +
                String("{\"window_id\":\"") + record->window_id +
                String("\",\"snapshot\":") + record->root->getRootSnapshotJson() +
                String("}");
        }
        return result + String("]");
    }

    int countWidgetMatches(
        const String& target,
        String* matched_window_id
    ) const {
        int matches = 0;

        if(matched_window_id) {
            *matched_window_id = String();
        }

        for(int i = 0; i < (int)windows.length(); i++) {
            WindowRecord* record = windows[i];
            if(!record || !record->root) {
                continue;
            }

            if(!targetOwnedByRoot(record->root, target)) {
                continue;
            }

            matches++;
            if(matched_window_id && matches == 1) {
                *matched_window_id = record->window_id;
            }
        }

        if(matched_window_id && matches != 1) {
            *matched_window_id = String();
        }

        return matches;
    }

    bool callWindow(
        const String& window_id,
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        WindowRecord* record = findWindow(window_id);

        if(!record || !record->root || !record->protocol) {
            error_code = "window_not_found";
            error_message = "No open window matched the requested window id";
            return false;
        }

        ElaraUiRpcUiService executor(record->root, record->protocol);
        return executor.call(method, params_json, result_json, error_code, error_message);
    }

    bool tryCallSecondary(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        for(int i = 0; i < (int)windows.length(); i++) {
            WindowRecord* record = windows[i];
            if(!record || !record->root || !record->protocol) {
                continue;
            }

            ElaraUiRpcUiService executor(record->root, record->protocol);
            String r, ec, em;
            if(executor.call(method, params_json, r, ec, em)) {
                result_json = r;
                return true;
            }

            if(!(ec == String("widget_not_found"))) {
                error_code = ec;
                error_message = em;
                return false;
            }
        }

        error_code = "widget_not_found";
        error_message = "No widget matched the requested target id in any window";
        return false;
    }
};

class MainThreadUiService : public sockets::rpc::json::JsonRPCService {
private:
    ElaraRootWidget* root;
    ElaraGuiBackend* backend;
    ElaraUiRpcUiService executor;
    ElaraJsonUiProtocol* protocol;
    SecondaryWindowManager* window_manager;
    Mutex queue_lock;
    Array< Ref<DeferredUiRequest> > queue;
    EventArtifactLogger* logger;
    bool layout_loaded;
    String loaded_window_title;
    int loaded_window_width;
    int loaded_window_height;
    int loaded_window_min_width;
    int loaded_window_min_height;
    bool loaded_window_use_system_header;

    bool dispatchWidgetTargetedCall(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        Json params(params_json);
        String target = params.getStringValue("target").trim();
        String window_id = params.getStringValue("window_id").trim();
        String target_window_id = targetWindowId(target);

        if(window_id.length() > 0 && target_window_id.length() > 0 && window_id != target_window_id) {
            error_code = "invalid_target_window";
            error_message = "The requested target widget id does not belong to the requested window_id";
            return false;
        }

        String requested_window_id = window_id.length() > 0 ? window_id : target_window_id;

        if(requested_window_id.length() > 0) {
            if(root && root->getRootId() == requested_window_id) {
                return executor.call(method, params_json, result_json, error_code, error_message);
            }

            if(window_manager && window_manager->hasWindow(requested_window_id)) {
                return window_manager->callWindow(
                    requested_window_id,
                    method,
                    params_json,
                    result_json,
                    error_code,
                    error_message
                );
            }

            error_code = "window_not_found";
            error_message = "No open window matched the requested window id";
            return false;
        }

        if(target.length() <= 0) {
            return executor.call(method, params_json, result_json, error_code, error_message);
        }

        bool main_matches = targetOwnedByRoot(root, target);
        String matched_window_id;
        int secondary_matches = window_manager
            ? window_manager->countWidgetMatches(target, &matched_window_id)
            : 0;
        int matches = (main_matches ? 1 : 0) + secondary_matches;

        if(matches > 1) {
            error_code = "ambiguous_widget_target";
            error_message = "Multiple windows matched the requested target id; specify window_id or use a qualified widget id";
            return false;
        }

        if(main_matches) {
            return executor.call(method, params_json, result_json, error_code, error_message);
        }

        if(matched_window_id.length() > 0 && window_manager) {
            return window_manager->callWindow(
                matched_window_id,
                method,
                params_json,
                result_json,
                error_code,
                error_message
            );
        }

        error_code = "widget_not_found";
        error_message = "No widget matched the requested target id";
        return false;
    }

public:
    MainThreadUiService(
        ElaraRootWidget* root,
        ElaraGuiBackend* gui_backend,
        ElaraJsonUiProtocol* ui_protocol,
        EventArtifactLogger* event_logger,
        SecondaryWindowManager* secondary_window_manager
    )
        : sockets::rpc::json::JsonRPCService("ui"),
          root(root),
          backend(gui_backend),
          executor(root, ui_protocol),
          protocol(ui_protocol),
          window_manager(secondary_window_manager),
          queue_lock("main-thread-ui-service"),
          logger(event_logger),
          layout_loaded(false),
          loaded_window_width(0),
          loaded_window_height(0),
          loaded_window_min_width(0),
          loaded_window_min_height(0),
          loaded_window_use_system_header(true) {
    }

    void notify(const String& method, const String& params_json) override {
        Ref<DeferredUiRequest> request(new DeferredUiRequest(method, params_json));
        Mutex::Lock lock(queue_lock);
        queue.push(request);
    }

    bool hasLoadedLayout() const {
        return layout_loaded;
    }

    String getLoadedWindowTitle() const { return loaded_window_title; }
    int getLoadedWindowWidth() const { return loaded_window_width; }
    int getLoadedWindowHeight() const { return loaded_window_height; }
    int getLoadedWindowMinWidth() const { return loaded_window_min_width; }
    int getLoadedWindowMinHeight() const { return loaded_window_min_height; }
    bool getLoadedWindowUseSystemHeader() const { return loaded_window_use_system_header; }

    bool loadDocument(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        Json params(params_json);
        String document = params.getStringValue("document");

        if(document.length() <= 0) {
            error_code = "missing_document";
            error_message = "ui.loadDocument requires a JSON document string";
            return false;
        }

        Json doc_json(document);
        String win_title = doc_json.getStringValue("window.title");
        int win_width = doc_json.getIntValue("window.width");
        int win_height = doc_json.getIntValue("window.height");
        int win_min_w = doc_json.getIntValue("window.min_width");
        int win_min_h = doc_json.getIntValue("window.min_height");
        bool win_use_system_header = jsonBoolValue(doc_json, "window.use_system_header", true);

        if(!protocol || !protocol->load(document)) {
            error_code = "load_failed";
            error_message = "The UI document could not be loaded";
            return false;
        }

        if(backend) {
            backend->invalidate();
        }

        loaded_window_title = win_title;
        loaded_window_width = win_width > 0 ? win_width : 800;
        loaded_window_height = win_height > 0 ? win_height : 600;
        loaded_window_min_width = win_min_w;
        loaded_window_min_height = win_min_h;
        loaded_window_use_system_header = win_use_system_header;
        layout_loaded = true;
        result_json = "{\"loaded\":true}";
        return true;
    }

    bool openWindow(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        if(!window_manager) {
            error_code = "unsupported_operation";
            error_message = "window manager is not available";
            return false;
        }

        Json params(params_json);
        String window_id = params.getStringValue("window_id");
        String title = params.getStringValue("title");
        String document = params.getStringValue("document");
        int width = params.getIntValue("width");
        int height = params.getIntValue("height");

        if(window_id.length() <= 0) {
            error_code = "missing_window_id";
            error_message = "ui.openWindow requires a window_id";
            return false;
        }

        if(title.length() <= 0) {
            title = "Elara Window";
        }

        if(width <= 0) {
            width = 640;
        }

        if(height <= 0) {
            height = 480;
        }

        if(document.length() <= 0) {
            error_code = "missing_document";
            error_message = "ui.openWindow requires a JSON document string";
            return false;
        }

        if(!window_manager->openWindow(window_id, title, width, height, document, error_message)) {
            error_code = "open_window_failed";
            return false;
        }

        result_json = "{\"opened\":true}";
        return true;
    }

    bool closeWindow(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        if(!window_manager) {
            error_code = "unsupported_operation";
            error_message = "window manager is not available";
            return false;
        }

        Json params(params_json);
        String window_id = params.getStringValue("window_id");

        if(window_id.length() <= 0) {
            error_code = "missing_window_id";
            error_message = "ui.closeWindow requires a window_id";
            return false;
        }

        if(!window_manager->closeWindow(window_id)) {
            error_code = "window_not_found";
            error_message = "No open window matched the requested id";
            return false;
        }

        result_json = "{\"closed\":true}";
        return true;
    }

    bool setWindowTitle(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        (void)error_code;
        (void)error_message;
        Json params(params_json);
        String title = params.getStringValue("title");
        if(window_manager) {
            window_manager->setMainWindowTitle(title);
        }
        result_json = "{\"ok\":true}";
        return true;
    }

    bool addDemoVectorOverlay(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        if (!root) {
            error_code = "missing_root";
            error_message = "root widget is not available";
            return false;
        }

        Json params(params_json);
        int ix = params.getIntValue("x");
        int iy = params.getIntValue("y");
        float x = (ix == 0 && iy == 0) ? 560.0f : (float)ix;
        float y = (ix == 0 && iy == 0) ? 10.0f  : (float)iy;

        ElaraVectorDocument overlay;
        overlay.setDocument(buildDemoOverlay());
        overlay.setPosition(x, y);
        root->addVectorOverlay("debug.demo", overlay);

        result_json = "{\"added\":true}";
        return true;
    }

    bool clearVectorOverlays(
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        (void)params_json;
        (void)error_code;
        (void)error_message;

        if (root) {
            root->clearVectorOverlays();
        }

        result_json = "{\"cleared\":true}";
        return true;
    }

    bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        Ref<DeferredUiRequest> request(new DeferredUiRequest(method, params_json));

        {
            Mutex::Lock lock(queue_lock);
            queue.push(request);
        }

        if(logger) {
            logger->log("rpc.enqueue", method + String(" params=") + params_json);
        }

        while(true) {
            {
                Mutex::Lock lock(request->mutex);
                if(request->completed) {
                    result_json = request->result_json;
                    error_code = request->error_code;
                    error_message = request->error_message;
                    return request->ok;
                }
            }

            usleep(1000);
        }
    }

    bool processPending() {
        bool processed = false;

        while(true) {
            Ref<DeferredUiRequest> request;

            {
                Mutex::Lock lock(queue_lock);

                if(queue.length() <= 0) {
                    break;
                }

                request = queue[0];
                queue.remove(0);
            }

            if(!request) {
                continue;
            }

            String result_json;
            String error_code;
            String error_message;
            bool ok = false;

            if(request->method == String("loadDocument")) {
                ok = loadDocument(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("openWindow")) {
                ok = openWindow(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("closeWindow")) {
                ok = closeWindow(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("setWindowTitle")) {
                ok = setWindowTitle(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("addDemoVectorOverlay")) {
                ok = addDemoVectorOverlay(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("clearVectorOverlays")) {
                ok = clearVectorOverlays(
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else if(request->method == String("snapshot") && window_manager) {
                String main_snapshot = root ? root->getRootSnapshotJson() : String("null");
                String secondary_snapshots = window_manager->snapshotAll();
                result_json =
                    String("{\"main\":") + main_snapshot +
                    String(",\"windows\":") + secondary_snapshots +
                    String("}");
                ok = true;
            } else if(targetsSpecificWidget(request->method)) {
                ok = dispatchWidgetTargetedCall(
                    request->method,
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
            } else {
                ok = executor.call(
                    request->method,
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );

                if(!ok && !(error_code == String("method_not_found")) && window_manager) {
                    ok = window_manager->tryCallSecondary(
                        request->method,
                        request->params_json,
                        result_json,
                        error_code,
                        error_message
                    );
                }
            }

            if(logger) {
                if(ok) {
                    logger->log("rpc.complete", request->method + String(" ok result=") + result_json);
                } else {
                    logger->log("rpc.complete", request->method + String(" error[") + error_code + String("] ") + error_message);
                }
            }

            {
                Mutex::Lock lock(request->mutex);
                request->result_json = result_json;
                request->error_code = error_code;
                request->error_message = error_message;
                request->ok = ok;
                request->completed = true;
            }

            processed = true;
        }

        return processed;
    }
};

class EventResponderService : public sockets::rpc::json::JsonRPCService {
public:
    EventResponderService()
        : sockets::rpc::json::JsonRPCService("event") {
    }

    void notify(const String& method, const String& params_json) override {
        String result, ec, em;
        call(method, params_json, result, ec, em);
    }

    bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) override {
        ElaraEventResponderTable* table = ElaraEventResponderTable::getInstance();

        if (method == String("setResponse")) {
            Json params(params_json);
            String event = params.getStringValue("event");
            String prefix = params.getStringValue("prefix");
            if (!event.length()) {
                error_code = "missing_event";
                error_message = "event.setResponse requires an event name";
                return false;
            }
            table->setResponse(event, prefix, parseCmds(params, "enter"), parseCmds(params, "leave"));
            result_json = "{\"ok\":true}";
            return true;
        }

        if (method == String("setNotify")) {
            Json params(params_json);
            String event = params.getStringValue("event");
            String prefix = params.getStringValue("prefix");
            if (!event.length()) {
                error_code = "missing_event";
                error_message = "event.setNotify requires an event name";
                return false;
            }
            String notify_str = params.getStringValue("notify");
            bool notify = !(notify_str == String("false") || notify_str == String("0"));
            table->setNotify(event, prefix, notify);
            result_json = "{\"ok\":true}";
            return true;
        }

        if (method == String("clearAll")) {
            table->clearAll();
            result_json = "{\"cleared\":true}";
            return true;
        }

        error_code = "method_not_found";
        error_message = "Unknown event service method";
        return false;
    }

private:
    static Array<ElaraResponderCmd> parseCmds(const Json& params, const String& field) {
        Array<ElaraResponderCmd> cmds;
        Array< Ref<JsonValue> > items = params.getArray(field);
        for (int i = 0; i < (int)items.length(); i++) {
            if (!items[i].getPtr()) continue;
            Json item(items[i]->toString());
            ElaraResponderCmd cmd;
            cmd.op = item.getStringValue("op");
            cmd.target = item.getStringValue("target");
            cmd.value = item.getStringValue("value");
            cmds.push(cmd);
        }
        return cmds;
    }
};

class UiRpcHost {
private:
    Ref<ElaraDrawSurface> root_surface;
    ElaraRootWidget* root;
    Ref<ElaraGuiBackend> backend;
    ElaraTheme* theme;
    ElaraJsonUiProtocol* protocol;
    EventArtifactLogger logger;
    SecondaryWindowManager window_manager;
    Ref<MainThreadUiService> ui_service_ref;
    Ref<sockets::rpc::json::JsonRPCService> rpc_ui_service_ref;
    MainThreadUiService* ui_service;
    Ref<ElaraUiRpcPeer> peer;
    Ref<ElaraUiRpcUiBridge> ui_bridge;
    Ref<WidgetListener> trace_listener;
    bool layout_attached;
    bool persistent;
    bool client_connected_once;
    bool use_brpc;
    DebugEventLog* debug_log;

    int listen_fd;
    bool running;
    pthread_t accept_thread;

    // Deferred listen: filled by queueListen(), started by first GTK tick.
    String deferred_listen_addr;
    unsigned short deferred_listen_port;
    bool deferred_listen_pending;

public:
    UiRpcHost(
        Ref<ElaraDrawSurface> surface,
        Ref<ElaraGuiBackend> gui_backend,
        ElaraTheme* ui_theme,
        ElaraJsonUiProtocol* ui_protocol,
        bool brpc = true
    )
        : root_surface(surface),
          root((ElaraRootWidget*)surface.getPtr()),
          backend(gui_backend),
          theme(ui_theme),
          protocol(ui_protocol),
          logger(),
          window_manager(gui_backend, ui_theme, &logger),
          ui_service_ref(new MainThreadUiService(root, gui_backend.getPtr(), protocol, &logger, &window_manager)),
          rpc_ui_service_ref(Ref<sockets::rpc::json::JsonRPCService>::borrow((sockets::rpc::json::JsonRPCService*)ui_service_ref.getPtr())),
          ui_service((MainThreadUiService*)ui_service_ref.getPtr()),
          layout_attached(false),
          persistent(false),
          client_connected_once(false),
          use_brpc(brpc),
          debug_log(0),
          listen_fd(0),
          running(false),
          accept_thread(0),
          deferred_listen_pending(false),
          deferred_listen_port(0) {
        ElaraEventResponderTable::getInstance(); // prime singleton before accept thread starts
    }

    void setPersistent(bool value) {
        persistent = value;
    }

    bool queueListen(const String& bind_address, unsigned short port) {
        deferred_listen_addr = bind_address;
        deferred_listen_port = port;
        deferred_listen_pending = true;
        return true;
    }

    ~UiRpcHost() {
        stop();
    }

    String getSessionDir() const {
        return logger.getSessionDir();
    }

    void setEventLogPath(const char* path) {
        if(debug_log) {
            delete debug_log;
        }
        debug_log = new DebugEventLog(path);
        if(!debug_log->isOpen()) {
            printf("warning: could not open event log at %s\n", path);
            delete debug_log;
            debug_log = 0;
        } else {
            printf("event log: %s\n", path);
        }
    }

    void installEventTracing() {
        if(trace_listener) {
            return;
        }

        trace_listener = Ref<WidgetListener>(new EventTraceListener(&logger));
        attachTraceRecursive(root);
        logger.log("artifact", String("session_dir=") + logger.getSessionDir());
    }

    void onLayoutLoaded() {
        if(layout_attached) {
            return;
        }

        installEventTracing();

        layout_attached = true;

        if(backend && ui_service) {
            String title = ui_service->getLoadedWindowTitle();
            int w = ui_service->getLoadedWindowWidth();
            int h = ui_service->getLoadedWindowHeight();
            int min_w = ui_service->getLoadedWindowMinWidth();
            int min_h = ui_service->getLoadedWindowMinHeight();
            if(title.length() > 0) {
                backend->setWindowTitle(title);
            }
            backend->setWindowDecorated(ui_service->getLoadedWindowUseSystemHeader());
            backend->setDefaultWindowSize(w, h);
            if(min_w > 0 || min_h > 0) {
                backend->setMinimumSize(min_w, min_h);
            }
            backend->showSurface(root_surface);
            backend->invalidate();
        }

        logger.log("ui.layout", "layout loaded over rpc");
    }

    bool listen(const String& bind_address, unsigned short port) {
        stop();

        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0) {
            return false;
        }

        int on = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if(bind_address.length() <= 0 || bind_address == String("0.0.0.0")) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else if(inet_pton(AF_INET, (const char*)bind_address, &addr.sin_addr) != 1) {
            ::close(listen_fd);
            listen_fd = 0;
            return false;
        }

        if(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            ::close(listen_fd);
            listen_fd = 0;
            return false;
        }

        if(::listen(listen_fd, 8) != 0) {
            ::close(listen_fd);
            listen_fd = 0;
            return false;
        }

        running = true;

        if(pthread_create(&accept_thread, 0, UiRpcHost::acceptThreadEntry, this) != 0) {
            running = false;
            ::close(listen_fd);
            listen_fd = 0;
            return false;
        }

        return true;
    }

    void stop() {
        running = false;

        if(listen_fd > 0) {
            shutdown(listen_fd, SHUT_RDWR);
            ::close(listen_fd);
            listen_fd = 0;
        }

        if(accept_thread) {
            pthread_join(accept_thread, 0);
            accept_thread = 0;
        }

        if(peer && peer->isConnected()) {
            String result, ec, em;
            peer->call(String("ui.quit"), String("{}"), result, ec, em, 200);
        }
        if(peer) {
            peer->close();
            peer = Ref<ElaraUiRpcPeer>(0);
            ui_bridge = Ref<ElaraUiRpcUiBridge>(0);
        }
    }

    bool tick() {
        if(deferred_listen_pending) {
            deferred_listen_pending = false;
            if(!listen(deferred_listen_addr, deferred_listen_port)) {
                logger.log("rpc.connection", "failed to start deferred RPC listen");
                if(backend) { backend->quit(); }
                return false;
            }
            logger.log("rpc.connection", String("listening for RPC clients on port ") + String((int)deferred_listen_port));
        }

        if(client_connected_once && peer && !peer->isConnected()) {
            if(!persistent) {
                logger.log("rpc.connection", "client disconnected — exiting");
                if(backend) {
                    backend->quit();
                }
                return false;
            }
            peer->close();
            peer = Ref<ElaraUiRpcPeer>(0);
            ui_bridge = Ref<ElaraUiRpcUiBridge>(0);
            client_connected_once = false;
            logger.log("rpc.connection", "client disconnected — persistent mode, waiting for reconnect");
        }

        bool dirty = false;
        if(ui_service) {
            dirty = ui_service->processPending();
            if(ui_service->hasLoadedLayout()) {
                onLayoutLoaded();
            }
        }
        if(ui_bridge && peer && peer->isConnected()) {
            if(ui_bridge->flushOutboundEvents(50)) {
                dirty = true;
            }
        }

        if(dirty && backend) {
            backend->invalidate();
        }

        return true;
    }

private:
    void attachClient(int fd) {
        if(fd <= 0) {
            return;
        }

        if(peer && peer->isConnected()) {
            logger.log("rpc.connection", String("rejecting extra connection fd=") + String(fd));
            ::close(fd);
            return;
        }

        if(peer) {
            peer->close();
        }

        peer = Ref<ElaraUiRpcPeer>(new ElaraUiRpcPeer());
        peer->setUseBrpc(use_brpc);
        peer->addService(rpc_ui_service_ref);
        peer->addService(Ref<sockets::rpc::json::JsonRPCService>(new EventResponderService()));

        if(peer->attach(fd)) {
            client_connected_once = true;
            ui_bridge = Ref<ElaraUiRpcUiBridge>(new ElaraUiRpcUiBridge(root, peer));
            if(debug_log) {
                ui_bridge->setEventSink(debug_log);
            }
            logger.log("rpc.connection", String("client connected fd=") + String(fd));
            root->enableOutboundEvent("mouseMove");
            root->enableOutboundEvent("mouseDown");
            root->enableOutboundEvent("mouseUp");
            root->enableOutboundEvent("clicked");
            root->enableOutboundEvent("hoverChanged");
            root->enableOutboundEvent("keyDown");
            root->enableOutboundEvent("keyUp");
            root->enableOutboundEvent("keysTyped");
            root->enableOutboundEvent("valueChanged");
            root->enableOutboundEvent("action");
            return;
        }

        ::close(fd);
        peer = Ref<ElaraUiRpcPeer>(0);
        ui_bridge = Ref<ElaraUiRpcUiBridge>(0);
    }

    void attachTraceRecursive(ElaraWidget* widget) {
        if(!widget || !trace_listener) {
            return;
        }

        widget->addListener(trace_listener);

        for(int i = 0; i < widget->childCount(); i++) {
            Ref<ElaraWidget> child = widget->getChild(i);
            attachTraceRecursive(child.getPtr());
        }
    }

    void acceptLoop() {
        while(running && listen_fd > 0) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);

            if(client_fd < 0) {
                if(errno == EINTR) {
                    continue;
                }

                if(!running) {
                    break;
                }

                usleep(50000);
                continue;
            }

            int flags = fcntl(client_fd, F_GETFL, 0);
            if(flags >= 0) {
                fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);
            }

            logger.log("rpc.connection", String("Attaching Client Connection") + String(client_fd));
            attachClient(client_fd);
        }
    }

    static void* acceptThreadEntry(void* instance) {
        ((UiRpcHost*)instance)->acceptLoop();
        return 0;
    }
};

struct WindowConfig {
    String title;
    int width;
    int height;
    String backend_id;

    WindowConfig()
        : title("libElaraUI RPC Debug"),
          width(800),
          height(600),
          backend_id("org.elara.ui.rpc.debug") {
    }
};

gboolean onHostTick(gpointer user_data) {
    UiRpcHost* host = (UiRpcHost*)user_data;
    return host->tick() ? TRUE : FALSE;
}

}

int main(int argc, char** argv) {
#ifndef WITH_GTK_BACKEND
    printf("libElaraUI RPC server requires GTK backend. Reconfigure without --disable-gtk.\n");
    return 1;
#else
    unsigned short rpc_port = 18777;
    const char* event_log_path = 0;
    bool persistent_mode = false;
    bool use_brpc = true;  // default: binary RPC
    WindowConfig window_config;

    /* Parse and strip our custom args so GTK never sees them. */
    {
        int out = 1;
        for(int i = 1; i < argc; i++) {
            if(strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                rpc_port = (unsigned short)atoi(argv[++i]);
            } else if(strcmp(argv[i], "--event-log") == 0 && i + 1 < argc) {
                event_log_path = argv[++i];
            } else if(strcmp(argv[i], "--persistent") == 0) {
                persistent_mode = true;
            } else if(strcmp(argv[i], "--json-rpc") == 0) {
                use_brpc = false;
            } else if(strcmp(argv[i], "--backend-id") == 0 && i + 1 < argc) {
                window_config.backend_id = String(argv[++i]);
            } else if(i == 1 && argv[i][0] != '-') {
                rpc_port = (unsigned short)atoi(argv[i]);
            } else {
                argv[out++] = argv[i];
            }
        }
        argc = out;
        argv[argc] = 0;
    }

    Ref<ElaraDrawSurface> root_surface(new ElaraRootWidget("main"));
    ElaraRootWidget* root = (ElaraRootWidget*)root_surface.getPtr();

    ElaraTheme theme;
    ElaraJsonUiProtocol protocol(root, &theme);

    Ref<ElaraGuiBackend> backend(new GtkGuiBackend(window_config.backend_id));
    if(root) {
        root->setGuiBackend(backend.getPtr());
    }
    UiRpcHost host(root_surface, backend, &theme, &protocol, use_brpc);

    if(event_log_path) {
        host.setEventLogPath(event_log_path);
    }

    if(persistent_mode) {
        host.setPersistent(true);
    }

    host.queueListen("0.0.0.0", rpc_port);

    g_timeout_add(16, onHostTick, &host);

    printf("libElaraUI RPC debug server starting, will listen on 0.0.0.0:%d after GTK init%s [codec: %s]\n",
           (int)rpc_port,
           persistent_mode ? " (persistent)" : " (exits on disconnect)",
           use_brpc ? "brpc" : "json");
    printf("event trace artifact: %s\n", (const char*)host.getSessionDir());

    ElaraWindow window(backend);
    window.setSurface(root_surface);
    window.create(window_config.title, window_config.width, window_config.height);
    return window.run(argc, argv);
#endif
}

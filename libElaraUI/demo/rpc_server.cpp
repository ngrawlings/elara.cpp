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

#include <libelaraui/config.h>
#include <libelaraui/ElaraGui.h>
#include <libelaraui/ElaraJsonUiProtocol.h>
#include <libelaraui/backends/gtk/GtkGuiBackend.h>
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
        fwrite((const char*)line, 1, line.byteLength(), event_log);
        fflush(event_log);
        event_count++;
        writeSummary();
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

class MainThreadUiService : public sockets::rpc::json::JsonRPCService {
private:
    ElaraUiRpcUiService executor;
    ElaraJsonUiProtocol* protocol;
    Mutex queue_lock;
    Array< Ref<DeferredUiRequest> > queue;
    EventArtifactLogger* logger;
    bool layout_loaded;

public:
    MainThreadUiService(
        ElaraRootWidget* root,
        ElaraJsonUiProtocol* ui_protocol,
        EventArtifactLogger* event_logger
    )
        : sockets::rpc::json::JsonRPCService("ui"),
          executor(root),
          protocol(ui_protocol),
          queue_lock("main-thread-ui-service"),
          logger(event_logger),
          layout_loaded(false) {
    }

    bool hasLoadedLayout() const {
        return layout_loaded;
    }

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

        if(!protocol || !protocol->load(document)) {
            error_code = "load_failed";
            error_message = "The UI document could not be loaded";
            return false;
        }

        layout_loaded = true;
        result_json = "{\"loaded\":true}";
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
            } else {
                ok = executor.call(
                    request->method,
                    request->params_json,
                    result_json,
                    error_code,
                    error_message
                );
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

class GridDemoLogicListener : public WidgetListener {
private:
    ElaraRootWidget* root;
    ElaraGuiBackend* backend;
    EventArtifactLogger* logger;

    String handleToString(ElaraWidgetHandle handle) const {
        Memory memory = handle.getHandle();
        return String((const char*)memory.getPtr(), memory.length());
    }

public:
    GridDemoLogicListener(ElaraRootWidget* root_widget, ElaraGuiBackend* gui_backend, EventArtifactLogger* event_logger)
        : root(root_widget),
          backend(gui_backend),
          logger(event_logger) {
    }

    void onWidgetClicked(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    ) {
        (void)button;
        (void)x;
        (void)y;

        const String button_handle("demo.widgets.button");
        const String input_handle("demo.widgets.input");
        String clicked_handle = handleToString(handle);

        if(logger) {
            logger->log("backend.logic", String("clicked handle=") + clicked_handle);
        }

        if(!(clicked_handle == button_handle)) {
            return;
        }

        if(logger) {
            logger->log("backend.logic", "button matched");
        }

        Ref<ElaraWidget> widget = root->getWidget(ElaraWidgetHandle(input_handle));

        if(logger) {
            logger->log(
                "backend.logic",
                String("input lookup widget=") + String(widget ? "found" : "missing")
            );
        }

        ElaraTextInputWidget* input = widget
            ? dynamic_cast<ElaraTextInputWidget*>(widget.getPtr())
            : 0;

        if(logger) {
            logger->log(
                "backend.logic",
                String("input cast=") + String(input ? "ok" : "failed")
            );
        }

        if(!input) {
            if(logger) {
                logger->log("backend.logic", "input lookup failed");
            }
            return;
        }

        String text = input->getText().trim();
        String before_text = text;

        if(logger) {
            logger->log("backend.logic", String("input text=") + before_text);
        }

        char* end = 0;
        long value = strtol((const char*)text, &end, 10);

        if(logger) {
            logger->log(
                "backend.logic",
                String("parse status end=") + String(end ? "set" : "null")
            );
        }

        if(!text.length() || !end || *end != 0) {
            value = 1;
        } else {
            value += 1;
        }

        input->setText(String((int)value));

        if(logger) {
            logger->log(
                "backend.logic",
                String("demo.widgets.button action=increment-input before=") +
                before_text +
                String(" after=") +
                String((int)value)
            );
        }

        if(backend) {
            backend->invalidate();
            if(logger) {
                logger->log("backend.logic", "invalidate requested");
            }
        }
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
    MainThreadUiService* ui_service;
    Ref<sockets::rpc::json::JsonRPCService> ui_service_ref;
    Ref<ElaraUiRpcPeer> peer;
    Ref<ElaraUiRpcUiBridge> ui_bridge;
    Ref<WidgetListener> demo_logic_listener;
    Ref<WidgetListener> trace_listener;
    bool layout_attached;

    int listen_fd;
    bool running;
    pthread_t accept_thread;

public:
    UiRpcHost(
        Ref<ElaraDrawSurface> surface,
        Ref<ElaraGuiBackend> gui_backend,
        ElaraTheme* ui_theme,
        ElaraJsonUiProtocol* ui_protocol
    )
        : root_surface(surface),
          root((ElaraRootWidget*)surface.getPtr()),
          backend(gui_backend),
          theme(ui_theme),
          protocol(ui_protocol),
          logger(),
          ui_service(new MainThreadUiService(root, protocol, &logger)),
          ui_service_ref((sockets::rpc::json::JsonRPCService*)ui_service),
          layout_attached(false),
          listen_fd(0),
          running(false),
          accept_thread(0) {
    }

    ~UiRpcHost() {
        stop();
    }

    String getSessionDir() const {
        return logger.getSessionDir();
    }

    void installDemoLogic() {
        if(demo_logic_listener) {
            return;
        }

        demo_logic_listener = Ref<WidgetListener>(new GridDemoLogicListener(root, backend.getPtr(), &logger));
        attachDemoLogicRecursive(root);
        logger.log("backend.logic", "listener attached");
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
        installDemoLogic();
        layout_attached = true;

        if(backend) {
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

        if(peer) {
            peer->close();
            peer = Ref<ElaraUiRpcPeer>(0);
            ui_bridge = Ref<ElaraUiRpcUiBridge>(0);
        }
    }

    bool tick() {
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

        if(peer) {
            peer->close();
        }

        peer = Ref<ElaraUiRpcPeer>(new ElaraUiRpcPeer());
        peer->addService(ui_service_ref);

        if(peer->attach(fd)) {
            ui_bridge = Ref<ElaraUiRpcUiBridge>(new ElaraUiRpcUiBridge(root, peer));
            logger.log("rpc.connection", String("client connected fd=") + String(fd));
            root->enableOutboundEvent("mouseMove");
            root->enableOutboundEvent("mouseDown");
            root->enableOutboundEvent("mouseUp");
            root->enableOutboundEvent("clicked");
            root->enableOutboundEvent("hoverChanged");
            root->enableOutboundEvent("keyDown");
            root->enableOutboundEvent("keyUp");
            root->enableOutboundEvent("keysTyped");
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

    void attachDemoLogicRecursive(ElaraWidget* widget) {
        if(!widget || !demo_logic_listener) {
            return;
        }

        widget->addListener(demo_logic_listener);

        for(int i = 0; i < widget->childCount(); i++) {
            Ref<ElaraWidget> child = widget->getChild(i);
            attachDemoLogicRecursive(child.getPtr());
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

    if(argc > 2) {
        rpc_port = (unsigned short)atoi(argv[2]);
    }

    WindowConfig window_config;

    Ref<ElaraDrawSurface> root_surface(new ElaraRootWidget());
    ElaraRootWidget* root = (ElaraRootWidget*)root_surface.getPtr();

    ElaraTheme theme;
    ElaraJsonUiProtocol protocol(root, &theme);

    Ref<ElaraGuiBackend> backend(new GtkGuiBackend(window_config.backend_id));
    UiRpcHost host(root_surface, backend, &theme, &protocol);

    if(!host.listen("0.0.0.0", rpc_port)) {
        printf("failed to listen for RPC clients on port %d\n", (int)rpc_port);
        return 1;
    }

    g_timeout_add(16, onHostTick, &host);

    printf("libElaraUI RPC debug server listening on 0.0.0.0:%d and waiting for ui.loadDocument\n",
           (int)rpc_port);
    printf("event trace artifact: %s\n", (const char*)host.getSessionDir());

    ElaraWindow window(backend);
    window.setSurface(root_surface);
    window.create(window_config.title, window_config.width, window_config.height);
    return window.run(argc, argv);
#endif
}

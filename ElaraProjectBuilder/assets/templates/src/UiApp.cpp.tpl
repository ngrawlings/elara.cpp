>>>>>>>>>>main>>>>CLASS_NAME>TITLE>BACKEND_ID>TARGET_NAME>IS_RICH_EDITOR>IS_VULKAN_SURFACE>IS_TABBED_PANEL
#include "%CLASS_NAME%.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;

%CLASS_NAME%::%CLASS_NAME%(const String &value_host, int value_port, const String &value_host_bridge_host, int value_host_bridge_port)
    : host(value_host),
      port(value_port),
      host_bridge_host(value_host_bridge_host),
      host_bridge_port(value_host_bridge_port),
      host_bridge_fd(-1),
      host_bridge_running(false),
      peer(new ElaraUiRpcPeer()) {
}

%CLASS_NAME%::~%CLASS_NAME%() {
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

void %CLASS_NAME%::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("%TITLE%"), 1080, 760, String("%BACKEND_ID%"));
    ui.setThemeMode(String("light"));
@include [%IS_RICH_EDITOR% == 1] UiApp.cpp.rich_editor_document>>>>%TITLE%
@include [%IS_VULKAN_SURFACE% == 1] UiApp.cpp.vulkan_surface_document>>>>%TITLE%>%BACKEND_ID%
@include [%IS_TABBED_PANEL% == 1] UiApp.cpp.tabbed_panel_document>>>>%TITLE%
}

bool %CLASS_NAME%::loadDocument(const String &document_json) {
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

bool %CLASS_NAME%::printSnapshot() {
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

bool %CLASS_NAME%::connectHostDebugBridge() {
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
    host_bridge_thread = std::thread(&%CLASS_NAME%::hostDebugBridgeLoop, this);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "\"project\":\"%TARGET_NAME%\",\"pid\":%ld,\"message\":\"%s\"",
             (long)getpid(),
             "%TITLE% C++ UI host registered");
    sendHostDebugEvent("register", payload);
    sendHostDebugEvent("state", "\"status\":\"C++ UI host ready\"");
    return true;
}

void %CLASS_NAME%::stopHostDebugBridge() {
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

void %CLASS_NAME%::hostDebugBridgeLoop() {
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

bool %CLASS_NAME%::sendHostDebugEvent(const char *kind, const char *payload) {
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

int %CLASS_NAME%::run() {
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
        printf("%TARGET_NAME%> ");
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
<<<<<<<<<<main

>>>>>>>>>>rich_editor_document>>>>TITLE
    ui.createTabs(String("app.tabs"));
    ui.setRootContent(String("app.tabs"));
    ui.createRichTextEdit(String("app.editor"), String("# %TITLE%\n\nThis template gives you a starting point for a document-oriented editor built on libElaraUI.\n\n- Connect backend actions over RPC\n- Extend the toolbar and outline tabs\n- Use snapshots to inspect state while iterating\n"));
    ui.setPropertyNumber(String("app.editor"), String("font_size"), 14);
    ui.addTab(String("app.tabs"), String("Editor"), String("app.editor"));
    ui.createListView(String("app.outline"));
    ui.setPropertyNumber(String("app.outline"), String("font_size"), 14);
    ui.setSectionJson(String("app.outline"), String("items"), String("[{\"id\":\"draft\",\"label\":\"Draft notes\"},{\"id\":\"tasks\",\"label\":\"Editing tasks\"},{\"id\":\"publish\",\"label\":\"Publishing checklist\"}]"));
    ui.addTab(String("app.tabs"), String("Outline"), String("app.outline"));
<<<<<<<<<<rich_editor_document

>>>>>>>>>>tabbed_panel_document>>>>TITLE
    ui.createTabs(String("app.tabs"));
    ui.setRootContent(String("app.tabs"));
    ui.createGrid(String("app.panel"));
    ui.addTab(String("app.tabs"), String("Control Panel"), String("app.panel"));
    ui.addGridColumnExact(String("app.panel"), 24);
    ui.addGridColumnFill(String("app.panel"));
    ui.addGridColumnExact(String("app.panel"), 220);
    ui.addGridRowExact(String("app.panel"), 24);
    ui.addGridRowExact(String("app.panel"), 44);
    ui.addGridRowExact(String("app.panel"), 44);
    ui.addGridRowExact(String("app.panel"), 44);
    ui.addGridRowFill(String("app.panel"));
    ui.addGridRowExact(String("app.panel"), 24);
    ui.createLabel(String("app.title"), String("%TITLE% control surface"), 18);
    ui.createTextInput(String("app.endpoint"), String("service endpoint"), String("https://api.example.local"));
    ui.createButton(String("app.refresh"), String("Refresh"), String("app.refresh"));
    ui.createCheckbox(String("app.live"), String("Live updates"), true);
    ui.setPropertyNumber(String("app.live"), String("font_size"), 14);
    ui.createSpinner(String("app.interval"), 1, 60, 5, 1);
    ui.setPropertyNumber(String("app.interval"), String("font_size"), 14);
    ui.createSlider(String("app.risk"), String("horizontal"), 0, 100, 35, 1);
    ui.createListView(String("app.activity"));
    ui.setPropertyNumber(String("app.activity"), String("font_size"), 14);
    ui.setSectionJson(String("app.activity"), String("items"), String("[{\"id\":\"queued\",\"label\":\"Queued refresh\"},{\"id\":\"connected\",\"label\":\"Connected to RPC head\"},{\"id\":\"ready\",\"label\":\"Ready for backend logic\"}]"));
    ui.placeGridChild(String("app.panel"), String("app.title"), 1, 1, 2, 1);
    ui.placeGridChild(String("app.panel"), String("app.endpoint"), 1, 2);
    ui.placeGridChild(String("app.panel"), String("app.refresh"), 2, 2);
    ui.placeGridChild(String("app.panel"), String("app.live"), 1, 3);
    ui.placeGridChild(String("app.panel"), String("app.interval"), 2, 3);
    ui.placeGridChild(String("app.panel"), String("app.risk"), 1, 4, 2, 1);
    ui.placeGridChild(String("app.panel"), String("app.activity"), 1, 5, 2, 1);
<<<<<<<<<<tabbed_panel_document

>>>>>>>>>>vulkan_surface_document>>>>TITLE>BACKEND_ID
    ui.createGrid(String("app.shell"));
    ui.addGridColumnFill(String("app.shell"));
    ui.addGridRowFill(String("app.shell"));
    ui.setRootContent(String("app.shell"));
    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("elara.os.desktop"));
    ui.setPropertyString(String("app.surface"), String("backend_id"), String("%BACKEND_ID%.surface"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("%TITLE%"));
    ui.setSectionJson(String("app.surface"), String("commands"), String("[{\"op\":\"clear\",\"r\":0.018,\"g\":0.021,\"b\":0.025},{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":720,\"r\":0.018,\"g\":0.021,\"b\":0.025},{\"op\":\"rect\",\"x\":0,\"y\":672,\"w\":1280,\"h\":48,\"r\":0.055,\"g\":0.06,\"b\":0.07},{\"op\":\"text\",\"x\":24,\"y\":696,\"text\":\"Elara OS Vulkan Surface Host\",\"size\":18,\"r\":0.86,\"g\":0.9,\"b\":0.95}]"));
    ui.placeGridChild(String("app.shell"), String("app.surface"), 0, 0);
<<<<<<<<<<vulkan_surface_document

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>

#include <libelaraformat/json/types/JsonString.h>
#include <libelaraio/File.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>

#include <libelarauirpc/ElaraUiRpcPeer.h>

using namespace elara;
using namespace elara::ui::rpc;

namespace {

class UiEventSinkService : public sockets::rpc::json::JsonRPCService {
public:
    UiEventSinkService()
        : sockets::rpc::json::JsonRPCService("ui") {
    }

    bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        if(method == String("event")) {
            printf("event %s\n", (const char*)params_json);
            result_json = "{\"received\":true}";
            return true;
        }

        error_code = "method_not_found";
        error_message = "No client-side ui event handler matched the request";
        return false;
    }
};

void trimLine(std::string* line) {
    while(!line->empty() && ((*line)[line->size() - 1] == '\n' || (*line)[line->size() - 1] == '\r')) {
        line->erase(line->size() - 1);
    }
}

bool startsWith(const std::string& value, const char* prefix) {
    size_t prefix_length = strlen(prefix);
    return value.size() >= prefix_length && value.compare(0, prefix_length, prefix) == 0;
}

String callUi(
    ElaraUiRpcPeer* peer,
    const String& method,
    const String& params_json
) {
    String result_json;
    String error_code;
    String error_message;

    if(peer->call(String("ui.") + method, params_json, result_json, error_code, error_message, 5000)) {
        return result_json;
    }

    printf("rpc error [%s] %s\n", (const char*)error_code, (const char*)error_message);
    return String();
}

String toJsonStringLiteral(const String& value) {
    return JsonString(value, true).toString();
}

void printHelp() {
    printf("usage:\n");
    printf("  libelarauirpc-client [host] [port] [layout.json] [--enable-default-events] [--cmd \"...\"] [--quit-after-commands]\n");
    printf("commands:\n");
    printf("  help\n");
    printf("  load-layout-file <path>\n");
    printf("  enable-default-events\n");
    printf("  snapshot\n");
    printf("  snapshot-widget <id>\n");
    printf("  enable-event <name>\n");
    printf("  disable-event <name>\n");
    printf("  mouse-move <x> <y>\n");
    printf("  mouse-down <button> <x> <y>\n");
    printf("  mouse-up <button> <x> <y>\n");
    printf("  click <x> <y> [button]\n");
    printf("  click-widget <id> [button]\n");
    printf("  key-down <keyval>\n");
    printf("  key-up <keyval>\n");
    printf("  type <text>\n");
    printf("  type-widget <id> <text>\n");
    printf("  wait <ms>\n");
    printf("  set-text <id> <text>\n");
    printf("  set-focus <id>\n");
    printf("  add-demo-overlay [x] [y]\n");
    printf("  clear-vector-overlays\n");
    printf("  show <json-method> <json-params>\n");
    printf("  quit\n");
}

bool readFileText(const char* path, String* out) {
    if(!path || !out) {
        return false;
    }

    File file(path);
    Memory data = file.getMemory();
    if(data.length() <= 0) {
        return false;
    }

    *out = String((const char*)data.getPtr(), data.length());
    return true;
}

void enableDefaultEvents(ElaraUiRpcPeer* peer) {
    static const char* event_names[] = {
        "mouseMove",
        "mouseDown",
        "mouseUp",
        "clicked",
        "hoverChanged",
        "keyDown",
        "keyUp",
        "keysTyped",
        "valueChanged",
        "action"
    };

    for(size_t i = 0; i < sizeof(event_names) / sizeof(event_names[0]); i++) {
        callUi(
            peer,
            "enableEvent",
            String("{\"action\":") + toJsonStringLiteral(String(event_names[i])) + String("}")
        );
    }
}

bool executeCommand(
    ElaraUiRpcPeer* peer,
    const std::string& line,
    bool* should_quit
) {
    if(line.empty()) {
        return true;
    }

    if(line == "quit" || line == "exit") {
        if(should_quit) {
            *should_quit = true;
        }
        return true;
    }

    if(line == "help") {
        printHelp();
        return true;
    }

    if(line == "enable-default-events") {
        enableDefaultEvents(peer);
        return true;
    }

    if(startsWith(line, "load-layout-file ")) {
        std::string path = line.substr(strlen("load-layout-file "));
        String document;

        if(!readFileText(path.c_str(), &document)) {
            printf("failed to read layout file: %s\n", path.c_str());
            return true;
        }

        String result = callUi(
            peer,
            "loadDocument",
            String("{\"document\":") + toJsonStringLiteral(document) + String("}")
        );
        if(result.length() > 0) {
            printf("%s\n", (const char*)result);
        }
        return true;
    }

    if(line == "snapshot") {
        String result = callUi(peer, "snapshot", "{}");
        if(result.length() > 0) {
            printf("%s\n", (const char*)result);
        }
        return true;
    }

    if(startsWith(line, "snapshot-widget ")) {
        std::string id = line.substr(strlen("snapshot-widget "));
        String result = callUi(
            peer,
            "snapshotWidget",
            String("{\"target\":") + toJsonStringLiteral(String(id.c_str())) + String("}")
        );
        if(result.length() > 0) {
            printf("%s\n", (const char*)result);
        }
        return true;
    }

    if(startsWith(line, "enable-event ")) {
        std::string event_name = line.substr(strlen("enable-event "));
        callUi(
            peer,
            "enableEvent",
            String("{\"action\":") + toJsonStringLiteral(String(event_name.c_str())) + String("}")
        );
        return true;
    }

    if(startsWith(line, "disable-event ")) {
        std::string event_name = line.substr(strlen("disable-event "));
        callUi(
            peer,
            "disableEvent",
            String("{\"action\":") + toJsonStringLiteral(String(event_name.c_str())) + String("}")
        );
        return true;
    }

    double x = 0;
    double y = 0;
    int button = 1;
    unsigned int keyval = 0;
    int wait_ms = 0;

    if(sscanf(line.c_str(), "mouse-move %lf %lf", &x, &y) == 2) {
        callUi(
            peer,
            "dispatchMouseMove",
            String("{\"x\":") + String(x) + String(",\"y\":") + String(y) + String("}")
        );
        return true;
    }

    if(sscanf(line.c_str(), "mouse-down %d %lf %lf", &button, &x, &y) == 3) {
        callUi(
            peer,
            "dispatchMouseDown",
            String("{\"button\":") + String(button) +
            String(",\"x\":") + String(x) +
            String(",\"y\":") + String(y) +
            String("}")
        );
        return true;
    }

    if(sscanf(line.c_str(), "mouse-up %d %lf %lf", &button, &x, &y) == 3) {
        callUi(
            peer,
            "dispatchMouseUp",
            String("{\"button\":") + String(button) +
            String(",\"x\":") + String(x) +
            String(",\"y\":") + String(y) +
            String("}")
        );
        return true;
    }

    if(startsWith(line, "click ")) {
        int parsed = sscanf(line.c_str(), "click %lf %lf %d", &x, &y, &button);
        if(parsed >= 2) {
            callUi(
                peer,
                "dispatchMouseDown",
                String("{\"button\":") + String(button) +
                String(",\"x\":") + String(x) +
                String(",\"y\":") + String(y) +
                String("}")
            );
            callUi(
                peer,
                "dispatchMouseUp",
                String("{\"button\":") + String(button) +
                String(",\"x\":") + String(x) +
                String(",\"y\":") + String(y) +
                String("}")
            );
        }
        return true;
    }

    if(startsWith(line, "click-widget ")) {
        size_t split = line.find(' ', strlen("click-widget "));
        std::string id;

        if(split == std::string::npos) {
            id = line.substr(strlen("click-widget "));
        } else {
            id = line.substr(strlen("click-widget "), split - strlen("click-widget "));
            button = atoi(line.substr(split + 1).c_str());
            if(button <= 0) {
                button = 1;
            }
        }

        callUi(
            peer,
            "clickWidget",
            String("{\"target\":") + toJsonStringLiteral(String(id.c_str())) +
            String(",\"button\":") + String(button) + String("}")
        );
        return true;
    }

    if(sscanf(line.c_str(), "key-down %u", &keyval) == 1) {
        callUi(
            peer,
            "dispatchKeyDown",
            String("{\"keyval\":") + String((int)keyval) + String("}")
        );
        return true;
    }

    if(sscanf(line.c_str(), "key-up %u", &keyval) == 1) {
        callUi(
            peer,
            "dispatchKeyUp",
            String("{\"keyval\":") + String((int)keyval) + String("}")
        );
        return true;
    }

    if(startsWith(line, "type ")) {
        std::string text = line.substr(strlen("type "));

        for(size_t i = 0; i < text.size(); i++) {
            unsigned int ch = (unsigned int)(unsigned char)text[i];
            callUi(
                peer,
                "dispatchKeyDown",
                String("{\"keyval\":") + String((int)ch) + String("}")
            );
            callUi(
                peer,
                "dispatchKeyUp",
                String("{\"keyval\":") + String((int)ch) + String("}")
            );
        }
        return true;
    }

    if(startsWith(line, "type-widget ")) {
        size_t split = line.find(' ', strlen("type-widget "));
        if(split != std::string::npos) {
            std::string id = line.substr(strlen("type-widget "), split - strlen("type-widget "));
            std::string text = line.substr(split + 1);
            callUi(
                peer,
                "typeWidget",
                String("{\"target\":") + toJsonStringLiteral(String(id.c_str())) +
                String(",\"text\":") + toJsonStringLiteral(String(text.c_str())) + String("}")
            );
        }
        return true;
    }

    if(sscanf(line.c_str(), "wait %d", &wait_ms) == 1) {
        if(wait_ms > 0) {
            usleep((useconds_t)wait_ms * 1000);
        }
        return true;
    }

    if(startsWith(line, "set-text ")) {
        size_t split = line.find(' ', strlen("set-text "));
        if(split != std::string::npos) {
            std::string id = line.substr(strlen("set-text "), split - strlen("set-text "));
            std::string text = line.substr(split + 1);
            String result = callUi(
                peer,
                "setText",
                String("{\"target\":") + toJsonStringLiteral(String(id.c_str())) +
                String(",\"value\":") + toJsonStringLiteral(String(text.c_str())) + String("}")
            );
            if(result.length() > 0) {
                printf("%s\n", (const char*)result);
            }
        }
        return true;
    }

    if(startsWith(line, "set-focus ")) {
        std::string id = line.substr(strlen("set-focus "));
        callUi(
            peer,
            "setFocus",
            String("{\"target\":") + toJsonStringLiteral(String(id.c_str())) + String("}")
        );
        return true;
    }

    if(line == "clear-vector-overlays") {
        String result = callUi(peer, "clearVectorOverlays", "{}");
        if(result.length() > 0) {
            printf("%s\n", (const char*)result);
        }
        return true;
    }

    if(startsWith(line, "add-demo-overlay")) {
        int ox = 0, oy = 0;
        sscanf(line.c_str(), "add-demo-overlay %d %d", &ox, &oy);
        char params[64];
        snprintf(params, sizeof(params), "{\"x\":%d,\"y\":%d}", ox, oy);
        String result = callUi(peer, "addDemoVectorOverlay", String(params));
        if(result.length() > 0) {
            printf("%s\n", (const char*)result);
        }
        return true;
    }

    if(startsWith(line, "show ")) {
        size_t split = line.find(' ', strlen("show "));
        if(split != std::string::npos) {
            std::string method = line.substr(strlen("show "), split - strlen("show "));
            std::string params = line.substr(split + 1);
            String result = callUi(peer, String(method.c_str()), String(params.c_str()));
            if(result.length() > 0) {
                printf("%s\n", (const char*)result);
            }
        }
        return true;
    }

    printf("unknown command\n");
    return false;
}

}

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    unsigned short port = 18777;
    const char* layout_path = 0;
    bool auto_enable_default_events = false;
    bool quit_after_commands = false;
    std::vector<std::string> startup_commands;

    if(argc > 1) {
        host = argv[1];
    }

    if(argc > 2) {
        port = (unsigned short)atoi(argv[2]);
    }

    if(argc > 3) {
        layout_path = argv[3];
    }

    for(int i = 4; i < argc; i++) {
        if(strcmp(argv[i], "--enable-default-events") == 0) {
            auto_enable_default_events = true;
            continue;
        }

        if(strcmp(argv[i], "--quit-after-commands") == 0) {
            quit_after_commands = true;
            continue;
        }

        if(strcmp(argv[i], "--cmd") == 0 && i + 1 < argc) {
            startup_commands.push_back(argv[++i]);
            continue;
        }
    }

    Ref<ElaraUiRpcPeer> peer(new ElaraUiRpcPeer());
    peer->addService(Ref<sockets::rpc::json::JsonRPCService>(new UiEventSinkService()));

    if(!peer->connect(host, port)) {
        printf("failed to connect to %s:%d\n", host, (int)port);
        return 1;
    }

    printf("connected to %s:%d\n", host, (int)port);

    if(layout_path && layout_path[0]) {
        String document;

        if(!readFileText(layout_path, &document)) {
            printf("failed to read layout file: %s\n", layout_path);
            peer->close();
            return 1;
        }

        String result = callUi(
            peer.getPtr(),
            "loadDocument",
            String("{\"document\":") + toJsonStringLiteral(document) + String("}")
        );

        if(result.length() <= 0) {
            peer->close();
            return 1;
        }

        printf("loaded layout file: %s\n", layout_path);
    }

    if(auto_enable_default_events) {
        enableDefaultEvents(peer.getPtr());
        printf("enabled default outbound UI events\n");
    }

    for(size_t i = 0; i < startup_commands.size(); i++) {
        printf("cmd> %s\n", startup_commands[i].c_str());
        bool should_quit = false;
        executeCommand(peer.getPtr(), startup_commands[i], &should_quit);
        if(should_quit) {
            peer->close();
            return 0;
        }
    }

    if(quit_after_commands && !startup_commands.empty()) {
        peer->close();
        return 0;
    }

    printHelp();

    while(true) {
        char buffer[4096];
        printf("> ");
        fflush(stdout);

        if(!fgets(buffer, sizeof(buffer), stdin)) {
            break;
        }

        std::string line(buffer);
        trimLine(&line);

        bool should_quit = false;
        executeCommand(peer.getPtr(), line, &should_quit);
        if(should_quit) {
            break;
        }
    }

    peer->close();
    return 0;
}

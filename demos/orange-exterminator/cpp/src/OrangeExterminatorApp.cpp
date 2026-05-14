#include "OrangeExterminatorApp.h"

#include <stdio.h>
#include <string.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;

OrangeExterminatorApp::OrangeExterminatorApp(const String &value_host, int value_port)
    : host(value_host),
      port(value_port),
      peer(new ElaraUiRpcPeer()) {
}

void OrangeExterminatorApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("OrangeExterminator"), 1080, 760, String("org.elara.ui.orange-exterminator"));
    ui.setThemeMode(String("light"));
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
    ui.createLabel(String("app.title"), String("OrangeExterminator control surface"), 18);
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
}

bool OrangeExterminatorApp::loadDocument(const String &document_json) {
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

bool OrangeExterminatorApp::printSnapshot() {
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

int OrangeExterminatorApp::run() {
    if (!peer->connect(host, (unsigned short)port)) {
        printf("Failed to connect to %s:%d\n", host.operator char *(), port);
        return 1;
    }
    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        return 1;
    }
    printf("Commands: reload, snapshot, quit\n");
    char line[256];
    while (true) {
        printf("orange-exterminator> ");
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
    return 0;
}

}

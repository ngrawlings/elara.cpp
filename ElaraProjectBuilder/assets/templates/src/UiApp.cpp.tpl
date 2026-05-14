>>>>>>>>>>main>>>>CLASS_NAME>TITLE>BACKEND_ID>TARGET_NAME>IS_RICH_EDITOR
#include "%CLASS_NAME%.h"

#include <stdio.h>
#include <string.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

namespace elara {
using namespace elara::ui::rpc;

%CLASS_NAME%::%CLASS_NAME%(const String &value_host, int value_port)
    : host(value_host),
      port(value_port),
      peer(new ElaraUiRpcPeer()) {
}

void %CLASS_NAME%::buildDocument(ElaraUiDocumentBuilder &ui) {
    ui.clear();
    ui.createWindow(String("%TITLE%"), 1080, 760, String("%BACKEND_ID%"));
    ui.setThemeMode(String("light"));
@include [%IS_RICH_EDITOR% == 1] UiApp.cpp.rich_editor_document>>>>%TITLE%
@include [%IS_RICH_EDITOR% == 0] UiApp.cpp.tabbed_panel_document>>>>%TITLE%
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

int %CLASS_NAME%::run() {
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

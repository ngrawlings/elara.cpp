#include "ElaraUiRpcUiService.h"

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonValue.h>

namespace elara {
namespace ui {
namespace rpc {

namespace {

String targetWindowId(const String& target) {
    String copy(target);
    int separator = copy.indexOf(String("::"));

    if(separator <= 0) {
        return String();
    }

    return copy.substr(0, separator).trim();
}

String requestedWindowId(const Json& params) {
    String window_id = params.getStringValue("window_id").trim();

    if(window_id.length() > 0) {
        return window_id;
    }

    return targetWindowId(params.getStringValue("target").trim());
}

double widgetEventX(Ref<ElaraWidget> widget) {
    if(!widget) {
        return 0;
    }

    double width = widget->getWidth();
    return width > 0 ? width / 2.0 : 0;
}

double widgetEventY(Ref<ElaraWidget> widget) {
    if(!widget) {
        return 0;
    }

    double height = widget->getHeight();
    return height > 0 ? height / 2.0 : 0;
}

}

namespace {

bool jsonBool(const Json& json, const String& path, bool fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text == String("true")) {
        return true;
    }

    if(text == String("false")) {
        return false;
    }

    return fallback;
}

double jsonNumber(const Json& json, const String& path, double fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text.length() <= 0) {
        return fallback;
    }

    return atof((const char*)text);
}

String jsonBoolean(bool value) {
    return value ? String("true") : String("false");
}

}

ElaraUiRpcUiService::ElaraUiRpcUiService(
    ElaraRootWidget* root_widget,
    ElaraJsonUiProtocol* ui_protocol
)
    : sockets::rpc::json::JsonRPCService("ui"),
      root(root_widget),
      protocol(ui_protocol) {
}

ElaraUiRpcUiService::~ElaraUiRpcUiService() {
}

Ref<ElaraWidget> ElaraUiRpcUiService::requireWidget(
    const Json& params,
    String& error_code,
    String& error_message
) const {
    String target = params.getStringValue("target").trim();
    String window_id = params.getStringValue("window_id").trim();
    String target_window_id = targetWindowId(target);

    if(target.length() <= 0) {
        error_code = "missing_target";
        error_message = "The ui method requires a target widget id";
        return Ref<ElaraWidget>();
    }

    if(window_id.length() > 0 && target_window_id.length() > 0 && window_id != target_window_id) {
        error_code = "invalid_target_window";
        error_message = "The requested target widget id does not belong to the requested window_id";
        return Ref<ElaraWidget>();
    }

    String requested_window_id = window_id.length() > 0 ? window_id : target_window_id;
    if(root && requested_window_id.length() > 0 && root->getRootId() != requested_window_id) {
        error_code = "widget_not_found";
        error_message = "No widget matched the requested target id in the requested window";
        return Ref<ElaraWidget>();
    }

    Ref<ElaraWidget> widget = root->getWidget(ElaraWidgetHandle(target));

    if(!widget) {
        error_code = "widget_not_found";
        error_message = requested_window_id.length() > 0
            ? String("No widget matched the requested target id in the requested window")
            : String("No widget matched the requested target id");
    }

    return widget;
}

bool ElaraUiRpcUiService::setText(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    String value = params.getStringValue("value");
    ElaraButtonWidget* button = dynamic_cast<ElaraButtonWidget*>(widget.getPtr());
    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    ElaraLabelWidget* label = dynamic_cast<ElaraLabelWidget*>(widget.getPtr());
    ElaraRichTextEditWidget* rich = dynamic_cast<ElaraRichTextEditWidget*>(widget.getPtr());
    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());
    ElaraComboBoxWidget* combo = dynamic_cast<ElaraComboBoxWidget*>(widget.getPtr());

    if(button) {
        button->setText(value);
    } else if(code_editor) {
        code_editor->setText(value);
    } else if(label) {
        label->setText(value);
    } else if(rich) {
        rich->setText(value);
    } else if(input) {
        input->setText(value);
    } else if(combo) {
        combo->setSelectedId(value);
    } else {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setText";
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setVisible(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    widget->setVisible(jsonBool(params, "visible", true));
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setEnabled(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    bool enabled = jsonBool(params, "enabled", true);
    ElaraButtonWidget* button = dynamic_cast<ElaraButtonWidget*>(widget.getPtr());
    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());

    if(button) {
        button->setEnabled(enabled);
    } else if(code_editor) {
        code_editor->setEnabled(enabled);
    } else if(input) {
        input->setEnabled(enabled);
    } else {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setEnabled";
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setReadOnly(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    if(!code_editor) {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setReadOnly";
        return false;
    }

    code_editor->setReadOnly(jsonBool(params, "read_only", false));
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setCodeEditorDiagnostics(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    if(!code_editor) {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support code editor diagnostics";
        return false;
    }

    code_editor->clearDiagnostics();
    Array< Ref<JsonValue> > diagnostics = params.getArray("diagnostics");
    for(int i = 0; i < (int)diagnostics.length(); i++) {
        Json diagnostic_json(diagnostics[i]->toString());
        code_editor->addDiagnostic(
            diagnostic_json.getIntValue("line"),
            diagnostic_json.getIntValue("column"),
            (int)jsonNumber(diagnostic_json, "length", 1),
            diagnostic_json.getStringValue("message")
        );
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setBounds(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    widget->setBounds(
        jsonNumber(params, "x", widget->getX()),
        jsonNumber(params, "y", widget->getY()),
        jsonNumber(params, "width", widget->getWidth()),
        jsonNumber(params, "height", widget->getHeight())
    );

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setFocus(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    root->setFocus(widget->getHandle());
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::enableEvent(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    String action = params.getStringValue("action");

    if(action.length() <= 0) {
        error_code = "missing_action";
        error_message = "The ui method requires an action name";
        return false;
    }

    root->enableOutboundEvent(action);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::disableEvent(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    String action = params.getStringValue("action");

    if(action.length() <= 0) {
        error_code = "missing_action";
        error_message = "The ui method requires an action name";
        return false;
    }

    root->disableOutboundEvent(action);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::clearChildren(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    if(!protocol) {
        error_code = "unsupported_operation";
        error_message = "Dynamic child-tree updates require a JSON UI protocol instance";
        return false;
    }

    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    if(!protocol->clearChildren(widget->getHandle())) {
        error_code = "widget_not_found";
        error_message = requestedWindowId(params).length() > 0
            ? String("No widget matched the requested target id in the requested window")
            : String("No widget matched the requested target id");
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::replaceChildren(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    if(!protocol) {
        error_code = "unsupported_operation";
        error_message = "Dynamic child-tree updates require a JSON UI protocol instance";
        return false;
    }

    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    String document = params.getStringValue("document");

    if(document.length() <= 0) {
        error_code = "missing_document";
        error_message = "The ui method requires a JSON subtree document string";
        return false;
    }

    if(!widget) {
        return false;
    }

    if(!protocol->replaceChildren(widget->getHandle(), document)) {
        error_code = "replace_failed";
        error_message = "The target widget children could not be replaced";
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::addTab(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    if(!protocol) {
        error_code = "unsupported_operation";
        error_message = "addTab requires a JSON UI protocol instance";
        return false;
    }

    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    String child_json = params.getStringValue("child");

    if(child_json.length() <= 0) {
        error_code = "missing_child";
        error_message = "addTab requires a child widget JSON string";
        return false;
    }

    String title = params.getStringValue("title");
    String btn_glyph = params.getStringValue("button_glyph");
    String btn_action = params.getStringValue("button_action");

    String spec_json = String("{\"title\":\"") + title
        + String("\",\"button_glyph\":\"") + btn_glyph
        + String("\",\"button_action\":\"") + btn_action
        + String("\",\"child\":") + child_json
        + String("}");

    if(!protocol->addTab(widget->getHandle(), spec_json)) {
        error_code = "add_tab_failed";
        error_message = "Failed to add tab";
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::removeTab(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    if(!protocol) {
        error_code = "unsupported_operation";
        error_message = "removeTab requires a JSON UI protocol instance";
        return false;
    }

    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    int index = params.getIntValue("index");

    if(index < 0) {
        error_code = "missing_index";
        error_message = "removeTab requires a non-negative tab index";
        return false;
    }

    protocol->removeTab(widget->getHandle(), index);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setActiveTab(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(widget.getPtr());

    if(!tabs) {
        error_code = "unsupported_widget";
        error_message = "setActiveTab target must be a tab widget";
        return false;
    }

    int index = params.getIntValue("index");
    tabs->setActiveTab(index);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchMouseMove(
    const Json& params,
    String& result_json
) {
    root->dispatchMouseMove(
        jsonNumber(params, "x", 0),
        jsonNumber(params, "y", 0)
    );
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchMouseDown(
    const Json& params,
    String& result_json
) {
    root->dispatchMouseDown(
        params.getIntValue("button"),
        jsonNumber(params, "x", 0),
        jsonNumber(params, "y", 0)
    );
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchMouseUp(
    const Json& params,
    String& result_json
) {
    root->dispatchMouseUp(
        params.getIntValue("button"),
        jsonNumber(params, "x", 0),
        jsonNumber(params, "y", 0)
    );
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::clickWidget(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    int button = params.getIntValue("button");
    if(button <= 0) {
        button = 1;
    }

    ElaraUiEvent move_event;
    move_event.root_widget = root;
    move_event.type = ELARA_UI_MOUSE_MOVE;
    move_event.x = widgetEventX(widget);
    move_event.y = widgetEventY(widget);

    ElaraUiEvent down_event = move_event;
    down_event.type = ELARA_UI_MOUSE_DOWN;
    down_event.button = button;

    ElaraUiEvent up_event = move_event;
    up_event.type = ELARA_UI_MOUSE_UP;
    up_event.button = button;

    widget->handleEvent(move_event);
    widget->handleEvent(down_event);
    widget->handleEvent(up_event);

    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchKeyDown(
    const Json& params,
    String& result_json
) {
    root->dispatchKeyDown(
        (unsigned int)params.getIntValue("keyval"),
        (unsigned int)params.getIntValue("modifiers")
    );
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchKeyUp(
    const Json& params,
    String& result_json
) {
    root->dispatchKeyUp(
        (unsigned int)params.getIntValue("keyval"),
        (unsigned int)params.getIntValue("modifiers")
    );
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::performAction(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    String action = params.getStringValue("action").trim();

    if(action.length() <= 0) {
        error_code = "missing_action";
        error_message = "The ui method requires an action name";
        return false;
    }

    if(!widget) {
        return false;
    }

    if(!widget->performAction(action)) {
        error_code = "unsupported_action";
        error_message = "The target widget does not support the requested action";
        return false;
    }

    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::performFocusedAction(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    String action = params.getStringValue("action").trim();

    if(action.length() <= 0) {
        error_code = "missing_action";
        error_message = "The ui method requires an action name";
        return false;
    }

    Ref<ElaraWidget> widget = root ? root->getWidget(root->getFocus()) : Ref<ElaraWidget>();

    if(!widget) {
        error_code = "widget_not_found";
        error_message = "No focused widget is available for the requested action";
        return false;
    }

    if(!widget->performAction(action)) {
        error_code = "unsupported_action";
        error_message = "The focused widget does not support the requested action";
        return false;
    }

    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::typeWidget(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    String text = params.getStringValue("text");
    if(text.length() <= 0) {
        error_code = "missing_text";
        error_message = "The ui method requires text to type";
        return false;
    }

    root->setFocus(widget->getHandle());

    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());
    if(input) {
        input->setFocused(true);
    }

    for(int i = 0; i < text.length(); i++) {
        unsigned int keyval = (unsigned int)(unsigned char)text.byteAt(i);
        root->dispatchKeyDown(keyval);
        root->dispatchKeyUp(keyval);
    }

    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::snapshot(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)params;
    (void)error_code;
    (void)error_message;

    result_json = root->getRootSnapshotJson();
    return true;
}

bool ElaraUiRpcUiService::snapshotWidget(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    result_json = root->getWidgetSnapshotJson(widget->getHandle());
    return true;
}

bool ElaraUiRpcUiService::call(
    const String& method,
    const String& params_json,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Json params(params_json);

    if(method == String("setText")) {
        return setText(params, result_json, error_code, error_message);
    }

    if(method == String("setVisible")) {
        return setVisible(params, result_json, error_code, error_message);
    }

    if(method == String("setEnabled")) {
        return setEnabled(params, result_json, error_code, error_message);
    }

    if(method == String("setReadOnly")) {
        return setReadOnly(params, result_json, error_code, error_message);
    }

    if(method == String("setCodeEditorDiagnostics")) {
        return setCodeEditorDiagnostics(params, result_json, error_code, error_message);
    }

    if(method == String("setBounds")) {
        return setBounds(params, result_json, error_code, error_message);
    }

    if(method == String("setFocus")) {
        return setFocus(params, result_json, error_code, error_message);
    }

    if(method == String("enableEvent")) {
        return enableEvent(params, result_json, error_code, error_message);
    }

    if(method == String("disableEvent")) {
        return disableEvent(params, result_json, error_code, error_message);
    }

    if(method == String("clearChildren")) {
        return clearChildren(params, result_json, error_code, error_message);
    }

    if(method == String("replaceChildren")) {
        return replaceChildren(params, result_json, error_code, error_message);
    }

    if(method == String("addTab")) {
        return addTab(params, result_json, error_code, error_message);
    }

    if(method == String("removeTab")) {
        return removeTab(params, result_json, error_code, error_message);
    }

    if(method == String("setActiveTab")) {
        return setActiveTab(params, result_json, error_code, error_message);
    }

    if(method == String("dispatchMouseMove")) {
        return dispatchMouseMove(params, result_json);
    }

    if(method == String("dispatchMouseDown")) {
        return dispatchMouseDown(params, result_json);
    }

    if(method == String("dispatchMouseUp")) {
        return dispatchMouseUp(params, result_json);
    }

    if(method == String("clickWidget")) {
        return clickWidget(params, result_json, error_code, error_message);
    }

    if(method == String("dispatchKeyDown")) {
        return dispatchKeyDown(params, result_json);
    }

    if(method == String("dispatchKeyUp")) {
        return dispatchKeyUp(params, result_json);
    }

    if(method == String("performAction")) {
        return performAction(params, result_json, error_code, error_message);
    }

    if(method == String("performFocusedAction")) {
        return performFocusedAction(params, result_json, error_code, error_message);
    }

    if(method == String("typeWidget")) {
        return typeWidget(params, result_json, error_code, error_message);
    }

    if(method == String("snapshot")) {
        return snapshot(params, result_json, error_code, error_message);
    }

    if(method == String("snapshotWidget")) {
        return snapshotWidget(params, result_json, error_code, error_message);
    }

    error_code = "method_not_found";
    error_message = "No ui rpc method matched the request";
    return false;
}

}
}
}

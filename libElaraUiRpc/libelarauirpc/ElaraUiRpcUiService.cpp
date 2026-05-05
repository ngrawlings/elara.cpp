#include "ElaraUiRpcUiService.h"

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonValue.h>

namespace elara {
namespace ui {
namespace rpc {

namespace {

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

ElaraUiRpcUiService::ElaraUiRpcUiService(ElaraRootWidget* root_widget)
    : sockets::rpc::json::JsonRPCService("ui"),
      root(root_widget) {
}

ElaraUiRpcUiService::~ElaraUiRpcUiService() {
}

Ref<ElaraWidget> ElaraUiRpcUiService::requireWidget(
    const Json& params,
    String& error_code,
    String& error_message
) const {
    String target = params.getStringValue("target");

    if(target.length() <= 0) {
        error_code = "missing_target";
        error_message = "The ui method requires a target widget id";
        return Ref<ElaraWidget>();
    }

    Ref<ElaraWidget> widget = root->getWidget(ElaraWidgetHandle(target));

    if(!widget) {
        error_code = "widget_not_found";
        error_message = "No widget matched the requested target id";
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
    ElaraLabelWidget* label = dynamic_cast<ElaraLabelWidget*>(widget.getPtr());
    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());

    if(button) {
        button->setText(value);
    } else if(label) {
        label->setText(value);
    } else if(input) {
        input->setText(value);
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
    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());

    if(button) {
        button->setEnabled(enabled);
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
    String target = params.getStringValue("target");

    if(target.length() <= 0) {
        error_code = "missing_target";
        error_message = "The ui method requires a target widget id";
        return false;
    }

    root->setFocus(ElaraWidgetHandle(target));
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
    root->dispatchKeyDown((unsigned int)params.getIntValue("keyval"));
    result_json = "{\"dispatched\":true}";
    return true;
}

bool ElaraUiRpcUiService::dispatchKeyUp(
    const Json& params,
    String& result_json
) {
    root->dispatchKeyUp((unsigned int)params.getIntValue("keyval"));
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

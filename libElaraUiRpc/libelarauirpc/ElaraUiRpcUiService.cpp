#include "ElaraUiRpcUiService.h"

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonValue.h>
#include <libelaraui/frontend/ElaraEventResponder.h>

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

double widgetAbsoluteEventX(Ref<ElaraWidget> widget) {
    if(!widget) {
        return 0;
    }

    return widget->getAbsoluteX() + widgetEventX(widget);
}

double widgetAbsoluteEventY(Ref<ElaraWidget> widget) {
    if(!widget) {
        return 0;
    }

    return widget->getAbsoluteY() + widgetEventY(widget);
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

double jsonValueDouble(Ref<JsonValue> value, double fallback) {
    if(!value) {
        return fallback;
    }

    String text = value->toString().trim();
    if(text.length() <= 0) {
        return fallback;
    }

    return atof((const char*)text);
}

double jsonValueDoubleEither(
    const Json& json,
    const String& primary_path,
    const String& fallback_path,
    double fallback
) {
    Ref<JsonValue> primary = json.getJsonValue(primary_path);

    if(primary && primary->toString().trim().length() > 0) {
        return jsonValueDouble(primary, fallback);
    }

    return jsonValueDouble(json.getJsonValue(fallback_path), fallback);
}

String jsonBoolean(bool value) {
    return value ? String("true") : String("false");
}

bool parseHexNibble(char c, int* value_out) {
    if(c >= '0' && c <= '9') {
        *value_out = c - '0';
        return true;
    }
    if(c >= 'a' && c <= 'f') {
        *value_out = 10 + (c - 'a');
        return true;
    }
    if(c >= 'A' && c <= 'F') {
        *value_out = 10 + (c - 'A');
        return true;
    }
    return false;
}

bool parseHexByte(const String& text, int offset, int* value_out) {
    int hi = 0;
    int lo = 0;
    const char* chars = (const char*)text;
    if(offset + 1 >= text.length()) {
        return false;
    }
    if(!parseHexNibble(chars[offset], &hi) || !parseHexNibble(chars[offset + 1], &lo)) {
        return false;
    }
    *value_out = (hi << 4) | lo;
    return true;
}

bool jsonColor(const Json& json, const String& path, ElaraColor* color_out) {
    String value = json.getStringValue(path).trim();
    const char* chars = (const char*)value;
    if((value.length() != 7 && value.length() != 9) || chars[0] != '#') {
        return false;
    }

    int r = 0, g = 0, b = 0, a = 255;
    if(!parseHexByte(value, 1, &r) || !parseHexByte(value, 3, &g) || !parseHexByte(value, 5, &b)) {
        return false;
    }
    if(value.length() == 9 && !parseHexByte(value, 7, &a)) {
        return false;
    }

    *color_out = ElaraColor(
        (double)r / 255.0,
        (double)g / 255.0,
        (double)b / 255.0,
        (double)a / 255.0
    );
    return true;
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
    ElaraChatDialogWidget* chat = dynamic_cast<ElaraChatDialogWidget*>(widget.getPtr());

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
    } else if(chat) {
        chat->setMessages(value);
    } else {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setText";
        return false;
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setCaretIndex(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if(!widget) return false;

    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    if(!code_editor) {
        error_code = "unsupported_widget";
        error_message = "setCaretIndex is only supported on code editor widgets";
        return false;
    }

    int idx = params.getIntValue("index");
    code_editor->setCaretIndex(idx);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::scrollToBottom(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraRichTextEditWidget* rich = dynamic_cast<ElaraRichTextEditWidget*>(widget.getPtr());

    if(rich) {
        rich->scrollToBottom();
        result_json = "{\"scrolled\":true}";
        return true;
    }

    error_code = "unsupported_widget";
    error_message = "The target widget does not support scrollToBottom";
    return false;
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

bool ElaraUiRpcUiService::setForegroundColor(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraColor color;
    if(!jsonColor(params, "color", &color)) {
        error_code = "invalid_color";
        error_message = "color must be a #RRGGBB or #RRGGBBAA string";
        return false;
    }

    widget->setForegroundColorOverride(color);
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

bool ElaraUiRpcUiService::setChecked(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraCheckboxWidget* checkbox = dynamic_cast<ElaraCheckboxWidget*>(widget.getPtr());
    if(!checkbox) {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setChecked";
        return false;
    }

    checkbox->setChecked(jsonBool(params, "checked", false));
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

bool ElaraUiRpcUiService::setEipLine(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if (!widget) return false;

    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    if (!code_editor) {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setEipLine";
        return false;
    }

    code_editor->setEipLine(params.getIntValue("line"));
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setEipPosition(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if (!widget) return false;

    ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget.getPtr());
    if (!code_editor) {
        error_code = "unsupported_widget";
        error_message = "The target widget does not support setEipPosition";
        return false;
    }

    code_editor->setEipPosition(params.getIntValue("line"), params.getIntValue("column"));
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
    widget->setFocused(true);
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::lockFocus(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    root->lockFocus(widget->getHandle());
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::clearFocusLock(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)params;
    (void)error_code;
    (void)error_message;

    root->clearFocusLock();
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setSectionJson(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    String section = params.getStringValue("section").trim();
    Ref<JsonValue> value = params.getJsonValue("value");
    String value_json = value ? value->toString() : String();

    if(section.length() <= 0) {
        error_code = "missing_section";
        error_message = "The ui method requires a section name";
        return false;
    }

    if(value_json.length() <= 0) {
        error_code = "missing_value";
        error_message = "The ui method requires a JSON value";
        return false;
    }

    ElaraListViewWidget* list = dynamic_cast<ElaraListViewWidget*>(widget.getPtr());
    if(list && section == String("items")) {
        Json spec(String("{\"items\":") + value_json + String("}"));
        Array< Ref<JsonValue> > items = spec.getArray("items");
        list->clearItems();
        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            list->addItem(ElaraListViewItem(
                item_json.getStringValue("id"),
                item_json.getStringValue("label")
            ));
        }
        if(root && root->getGuiBackend()) {
            root->getGuiBackend()->invalidate();
        }
        result_json = "{\"updated\":true}";
        return true;
    }

    ElaraComboBoxWidget* combo = dynamic_cast<ElaraComboBoxWidget*>(widget.getPtr());
    if(combo && section == String("items")) {
        Json spec(String("{\"items\":") + value_json + String("}"));
        Array< Ref<JsonValue> > items = spec.getArray("items");
        combo->clearItems();
        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            combo->addItem(
                item_json.getStringValue("id"),
                item_json.getStringValue("label")
            );
        }
        if(root && root->getGuiBackend()) {
            root->getGuiBackend()->invalidate();
        }
        result_json = "{\"updated\":true}";
        return true;
    }

    ElaraToolBarWidget* toolbar = dynamic_cast<ElaraToolBarWidget*>(widget.getPtr());
    if(toolbar && section == String("items")) {
        Json spec(String("{\"items\":") + value_json + String("}"));
        Array< Ref<JsonValue> > items = spec.getArray("items");
        toolbar->clearItems();
        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            if(jsonBool(item_json, "separator", false)) {
                toolbar->addSeparator();
                continue;
            }
            toolbar->addItem(
                item_json.getStringValue("id"),
                item_json.getStringValue("text"),
                item_json.getStringValue("icon"),
                jsonBool(item_json, "enabled", true),
                item_json.getStringValue("tooltip")
            );
        }
        if(root && root->getGuiBackend()) {
            root->getGuiBackend()->invalidate();
        }
        result_json = "{\"updated\":true}";
        return true;
    }

    ElaraOpenClSurfaceWidget* opencl_surface = dynamic_cast<ElaraOpenClSurfaceWidget*>(widget.getPtr());
    if(opencl_surface && section == String("commands")) {
        Json spec(String("{\"commands\":") + value_json + String("}"));
        Array< Ref<JsonValue> > commands = spec.getArray("commands");
        opencl_surface->clearCommands();
        for(int i = 0; i < (int)commands.length(); i++) {
            Json command_json(commands[i]->toString());
            String op = command_json.getStringValue("op");
            if(op == String("clear")) {
                opencl_surface->addClear(
                    jsonValueDouble(command_json.getJsonValue("r"), 0.10),
                    jsonValueDouble(command_json.getJsonValue("g"), 0.11),
                    jsonValueDouble(command_json.getJsonValue("b"), 0.14)
                );
            } else if(op == String("rect")) {
                opencl_surface->addRect(
                    jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("w"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("h"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            } else if(op == String("line")) {
                opencl_surface->addLine(
                    jsonValueDoubleEither(command_json, "x0", "x1", 0.0),
                    jsonValueDoubleEither(command_json, "y0", "y1", 0.0),
                    jsonValueDoubleEither(command_json, "x1", "x2", 0.0),
                    jsonValueDoubleEither(command_json, "y1", "y2", 0.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            } else if(op == String("text")) {
                opencl_surface->addText(
                    jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                    command_json.getStringValue("text"),
                    jsonValueDouble(command_json.getJsonValue("size"), 24.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            }
        }
        if(root && root->getGuiBackend()) {
            root->getGuiBackend()->invalidate();
        }
        result_json = "{\"updated\":true}";
        return true;
    }

    ElaraVulkanSurfaceWidget* vulkan_surface = dynamic_cast<ElaraVulkanSurfaceWidget*>(widget.getPtr());
    if(vulkan_surface && section == String("commands")) {
        Json spec(String("{\"commands\":") + value_json + String("}"));
        Array< Ref<JsonValue> > commands = spec.getArray("commands");
        vulkan_surface->clearCommands();
        for(int i = 0; i < (int)commands.length(); i++) {
            Json command_json(commands[i]->toString());
            String op = command_json.getStringValue("op");
            if(op == String("clear")) {
                vulkan_surface->addClear(
                    jsonValueDouble(command_json.getJsonValue("r"), 0.10),
                    jsonValueDouble(command_json.getJsonValue("g"), 0.11),
                    jsonValueDouble(command_json.getJsonValue("b"), 0.14)
                );
            } else if(op == String("rect")) {
                vulkan_surface->addRect(
                    jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("w"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("h"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            } else if(op == String("line")) {
                vulkan_surface->addLine(
                    jsonValueDoubleEither(command_json, "x0", "x1", 0.0),
                    jsonValueDoubleEither(command_json, "y0", "y1", 0.0),
                    jsonValueDoubleEither(command_json, "x1", "x2", 0.0),
                    jsonValueDoubleEither(command_json, "y1", "y2", 0.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            } else if(op == String("text")) {
                vulkan_surface->addText(
                    jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                    command_json.getStringValue("text"),
                    jsonValueDouble(command_json.getJsonValue("size"), 24.0),
                    jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                    jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                );
            } else if(op == String("scene")) {
                vulkan_surface->addSceneCommand(
                    (int)jsonValueDouble(command_json.getJsonValue("scene_op"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a0"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a1"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a2"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a3"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a4"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a5"), 0.0),
                    jsonValueDouble(command_json.getJsonValue("a6"), 0.0)
                );
            }
        }
        if(root && root->getGuiBackend()) {
            root->getGuiBackend()->invalidate();
        }
        result_json = "{\"updated\":true}";
        return true;
    }

    error_code = "unsupported_widget";
    error_message = "The target widget does not support setSectionJson for the requested section";
    return false;
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

bool ElaraUiRpcUiService::dispatchMouseScroll(
    const Json& params,
    String& result_json
) {
    root->dispatchMouseScroll(
        jsonNumber(params, "dx", 0),
        jsonNumber(params, "dy", 0),
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

    int button = (int)jsonNumber(params, "button", 1);
    if(button <= 0) {
        button = 1;
    }

    double x = widgetAbsoluteEventX(widget);
    double y = widgetAbsoluteEventY(widget);

    root->dispatchMouseMove(x, y);
    root->dispatchMouseDown(button, x, y);
    root->dispatchMouseUp(button, x, y);

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

bool ElaraUiRpcUiService::getGridLayoutState(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraGridLayout* grid = dynamic_cast<ElaraGridLayout*>(widget.getPtr());
    if(!grid) {
        error_code = "unsupported_widget";
        error_message = "The target widget is not a grid layout";
        return false;
    }

    String json = "{\"columns\":[";
    for(int i = 0; i < grid->columnCount(); i++) {
        if(i > 0) {
            json += ",";
        }
        ElaraGridTrack track = grid->columnTrack(i);
        json += String("{\"mode\":\"") +
            (track.mode == ELARA_GRID_SIZE_FILL ? String("fill") : String("exact")) +
            String("\",\"size\":") + String(track.size) +
            String(",\"weight\":") + String(track.weight) +
            String(",\"computed_size\":") + String(track.computed_size) +
            String(",\"computed_offset\":") + String(track.computed_offset) +
            String(",\"resizable_after\":") + jsonBoolean(track.resizable_after) +
            String("}");
    }
    json += "],\"rows\":[";
    for(int i = 0; i < grid->rowCount(); i++) {
        if(i > 0) {
            json += ",";
        }
        ElaraGridTrack track = grid->rowTrack(i);
        json += String("{\"mode\":\"") +
            (track.mode == ELARA_GRID_SIZE_FILL ? String("fill") : String("exact")) +
            String("\",\"size\":") + String(track.size) +
            String(",\"weight\":") + String(track.weight) +
            String(",\"computed_size\":") + String(track.computed_size) +
            String(",\"computed_offset\":") + String(track.computed_offset) +
            String(",\"resizable_after\":") + jsonBoolean(track.resizable_after) +
            String("}");
    }
    json += "]}";
    result_json = json;
    return true;
}

bool ElaraUiRpcUiService::setGridColumnExactSize(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraGridLayout* grid = dynamic_cast<ElaraGridLayout*>(widget.getPtr());
    if(!grid) {
        error_code = "unsupported_widget";
        error_message = "The target widget is not a grid layout";
        return false;
    }

    int index = (int)jsonNumber(params, "index", -1);
    if(index < 0 || index >= grid->columnCount()) {
        error_code = "invalid_column";
        error_message = "The requested grid column index is out of range";
        return false;
    }

    double size = jsonNumber(params, "size", 0.0);
    grid->setColumnExactSize(index, size);

    if(root && root->getGuiBackend()) {
        root->getGuiBackend()->invalidate();
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setGridRowExactSize(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);

    if(!widget) {
        return false;
    }

    ElaraGridLayout* grid = dynamic_cast<ElaraGridLayout*>(widget.getPtr());
    if(!grid) {
        error_code = "unsupported_widget";
        error_message = "The target widget is not a grid layout";
        return false;
    }

    int index = (int)jsonNumber(params, "index", -1);
    if(index < 0 || index >= grid->rowCount()) {
        error_code = "invalid_row";
        error_message = "The requested grid row index is out of range";
        return false;
    }

    double size = jsonNumber(params, "size", 0.0);
    grid->setRowExactSize(index, size);

    if(root && root->getGuiBackend()) {
        root->getGuiBackend()->invalidate();
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::getWindowState(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)params;
    (void)error_code;
    (void)error_message;

    bool maximized = false;
    int win_w = 0, win_h = 0;
    if(root && root->getGuiBackend()) {
        maximized = root->getGuiBackend()->isWindowMaximized();
        root->getGuiBackend()->getWindowSize(win_w, win_h);
    }

    result_json = String("{\"maximized\":") + jsonBoolean(maximized)
        + String(",\"width\":") + String(win_w)
        + String(",\"height\":") + String(win_h)
        + String("}");
    return true;
}

bool ElaraUiRpcUiService::setWindowSize(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)error_code;
    (void)error_message;

    int w = (int)jsonNumber(params, "width", 0.0);
    int h = (int)jsonNumber(params, "height", 0.0);
    if(w > 0 && h > 0 && root && root->getGuiBackend()) {
        root->getGuiBackend()->setWindowSize(w, h);
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setWindowMaximized(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)error_code;
    (void)error_message;

    if(root && root->getGuiBackend()) {
        root->getGuiBackend()->setWindowMaximized(jsonBool(params, "maximized", false));
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setWindowDecorated(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)error_code;
    (void)error_message;

    if(root && root->getGuiBackend()) {
        root->getGuiBackend()->setWindowDecorated(jsonBool(params, "decorated", true));
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setMouseCaptured(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    (void)error_code;
    (void)error_message;

    if(root && root->getGuiBackend()) {
        root->getGuiBackend()->setMouseCaptured(jsonBool(params, "captured", false));
    }

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::configureMenuBarChrome(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if(!widget) {
        return false;
    }

    ElaraMenuBarWidget* menu_bar = dynamic_cast<ElaraMenuBarWidget*>(widget.getPtr());
    if(!menu_bar) {
        error_code = "unsupported_widget";
        error_message = "configureMenuBarChrome target must be a menu bar widget";
        return false;
    }

    menu_bar->setCustomChrome(jsonBool(params, "custom_chrome", false));
    menu_bar->setWindowTitle(params.getStringValue("window_title"));
    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::setThemeMode(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    if(!protocol) {
        error_code = "no_protocol";
        error_message = "UI protocol not available";
        return false;
    }

    String mode = params.getStringValue("mode");
    if(mode.length() == 0) {
        error_code = "invalid_params";
        error_message = "setThemeMode requires a 'mode' parameter";
        return false;
    }

    if(!protocol->setThemeMode(mode)) {
        error_code = "invalid_mode";
        error_message = "Unknown theme mode";
        return false;
    }

    ElaraEventResponderTable::getInstance()->clearAll();

    result_json = "{\"updated\":true}";
    return true;
}

bool ElaraUiRpcUiService::spawnTerminalShell(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if (!widget) return false;

    ElaraTerminalWidget* terminal = dynamic_cast<ElaraTerminalWidget*>(widget.getPtr());
    if (!terminal) {
        error_code = "unsupported_widget";
        error_message = "Target widget is not a terminal widget";
        return false;
    }

    String cwd = params.getStringValue("cwd");
    terminal->spawn(cwd);
    result_json = "{\"spawned\":true}";
    return true;
}

bool ElaraUiRpcUiService::sendTerminalInput(
    const Json& params,
    String& result_json,
    String& error_code,
    String& error_message
) {
    Ref<ElaraWidget> widget = requireWidget(params, error_code, error_message);
    if (!widget) return false;

    ElaraTerminalWidget* terminal = dynamic_cast<ElaraTerminalWidget*>(widget.getPtr());
    if (!terminal) {
        error_code = "unsupported_widget";
        error_message = "Target widget is not a terminal widget";
        return false;
    }

    String data = params.getStringValue("data");
    terminal->sendInput((const char*)data, (int)data.length());
    result_json = "{\"sent\":true}";
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

    if(method == String("scrollToBottom")) {
        return scrollToBottom(params, result_json, error_code, error_message);
    }

    if(method == String("setText")) {
        return setText(params, result_json, error_code, error_message);
    }

    if(method == String("setCaretIndex")) {
        return setCaretIndex(params, result_json, error_code, error_message);
    }

    if(method == String("setVisible")) {
        return setVisible(params, result_json, error_code, error_message);
    }

    if(method == String("setForegroundColor")) {
        return setForegroundColor(params, result_json, error_code, error_message);
    }

    if(method == String("setEnabled")) {
        return setEnabled(params, result_json, error_code, error_message);
    }

    if(method == String("setChecked")) {
        return setChecked(params, result_json, error_code, error_message);
    }

    if(method == String("setReadOnly")) {
        return setReadOnly(params, result_json, error_code, error_message);
    }

    if(method == String("setCodeEditorDiagnostics")) {
        return setCodeEditorDiagnostics(params, result_json, error_code, error_message);
    }
    if(method == String("setEipLine")) {
        return setEipLine(params, result_json, error_code, error_message);
    }
    if(method == String("setEipPosition")) {
        return setEipPosition(params, result_json, error_code, error_message);
    }

    if(method == String("setBounds")) {
        return setBounds(params, result_json, error_code, error_message);
    }

    if(method == String("setFocus")) {
        return setFocus(params, result_json, error_code, error_message);
    }

    if(method == String("lockFocus")) {
        return lockFocus(params, result_json, error_code, error_message);
    }

    if(method == String("clearFocusLock")) {
        return clearFocusLock(params, result_json, error_code, error_message);
    }

    if(method == String("setSectionJson")) {
        return setSectionJson(params, result_json, error_code, error_message);
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

    if(method == String("dispatchMouseScroll")) {
        return dispatchMouseScroll(params, result_json);
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

    if(method == String("getGridLayoutState")) {
        return getGridLayoutState(params, result_json, error_code, error_message);
    }

    if(method == String("setGridColumnExactSize")) {
        return setGridColumnExactSize(params, result_json, error_code, error_message);
    }

    if(method == String("setGridRowExactSize")) {
        return setGridRowExactSize(params, result_json, error_code, error_message);
    }

    if(method == String("getWindowState")) {
        return getWindowState(params, result_json, error_code, error_message);
    }

    if(method == String("setWindowSize")) {
        return setWindowSize(params, result_json, error_code, error_message);
    }

    if(method == String("setWindowMaximized")) {
        return setWindowMaximized(params, result_json, error_code, error_message);
    }
    if(method == String("setWindowDecorated")) {
        return setWindowDecorated(params, result_json, error_code, error_message);
    }
    if(method == String("configureMenuBarChrome")) {
        return configureMenuBarChrome(params, result_json, error_code, error_message);
    }

    if(method == String("setThemeMode")) {
        return setThemeMode(params, result_json, error_code, error_message);
    }

    if(method == String("setMouseCaptured")) {
        return setMouseCaptured(params, result_json, error_code, error_message);
    }

    if(method == String("spawnTerminalShell")) {
        return spawnTerminalShell(params, result_json, error_code, error_message);
    }

    if(method == String("sendTerminalInput")) {
        return sendTerminalInput(params, result_json, error_code, error_message);
    }

    error_code = "method_not_found";
    error_message = "No ui rpc method matched the request";
    return false;
}

}
}
}

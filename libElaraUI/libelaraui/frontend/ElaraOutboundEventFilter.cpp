#include "ElaraOutboundEventFilter.h"

#include <libelaracore/memory/Memory.h>
#include <libelaraformat/json/types/JsonString.h>
#include "ElaraEventResponder.h"
#include "ElaraWidgetRegistry.h"

namespace elara {

static String handleWidgetId(ElaraWidgetHandle handle) {
    Memory mem = handle.getHandle();
    return String((const char*)mem.getPtr(), mem.length());
}

ElaraOutboundEventFilter::ElaraOutboundEventFilter() {}

void ElaraOutboundEventFilter::enable(const String& action) {
    whitelist.set(action, action);
}

void ElaraOutboundEventFilter::disable(const String& action) {
    Memory key = Memory((const char*)action, action.byteLength());
    whitelist.remove(key);
}

bool ElaraOutboundEventFilter::isEnabled(const String& action) const {
    Memory key = Memory((const char*)action, action.byteLength());
    return whitelist.get(key).getPtr() != 0;
}

void ElaraOutboundEventFilter::queue(
    ElaraWidgetHandle handle,
    const String& action,
    const String& payload
) {
    if(!isEnabled(action)) {
        return;
    }

    ElaraOutboundEventQueue::getInstance()->push(handle, action, payload);
}

String ElaraOutboundEventFilter::mousePayload(double x, double y) const {
    return String("{\"x\":") + String(x) +
           String(",\"y\":") + String(y) +
           String("}");
}

String ElaraOutboundEventFilter::buttonPayload(
    int button,
    double x,
    double y
) const {
    return String("{\"button\":") + String(button) +
           String(",\"x\":") + String(x) +
           String(",\"y\":") + String(y) +
           String("}");
}

String ElaraOutboundEventFilter::scrollPayload(double dx, double dy) const {
    return String("{\"dx\":") + String(dx) +
           String(",\"dy\":") + String(dy) +
           String("}");
}

String ElaraOutboundEventFilter::stringPayload(
    const String& field,
    const String& value
) const {
    return String("{\"") + field + String("\":\"") +
           JsonString::encode(value) +
           String("\"}");
}

void ElaraOutboundEventFilter::onWidgetMouseMove(
    ElaraWidgetHandle handle,
    double x,
    double y
) {
    Ref<ElaraWidget> widget = ElaraWidgetRegistry::getInstance()->getWidget(handle);
    if (widget.getPtr() && widget->isHoverOnly()) return;
    queue(handle, "mouseMove", mousePayload(x, y));
}

void ElaraOutboundEventFilter::onWidgetMouseDown(
    ElaraWidgetHandle handle,
    int button,
    double x,
    double y
) {
    queue(handle, "mouseDown", buttonPayload(button, x, y));
}

void ElaraOutboundEventFilter::onWidgetMouseUp(
    ElaraWidgetHandle handle,
    int button,
    double x,
    double y
) {
    queue(handle, "mouseUp", buttonPayload(button, x, y));
}

void ElaraOutboundEventFilter::onWidgetMouseScroll(
    ElaraWidgetHandle handle,
    double dx,
    double dy
) {
    queue(handle, "mouseScroll", scrollPayload(dx, dy));
}

void ElaraOutboundEventFilter::onWidgetClicked(
    ElaraWidgetHandle handle,
    int button,
    double x,
    double y
) {
    queue(handle, "clicked", buttonPayload(button, x, y));
}

void ElaraOutboundEventFilter::onWidgetHoverChanged(
    ElaraWidgetHandle handle,
    bool hovered
) {
    if (!isEnabled("hoverChanged")) return;

    String widget_id = handleWidgetId(handle);
    ElaraEventResponderTable* table = ElaraEventResponderTable::getInstance();
    if (hovered) table->applyEnter("hoverChanged", widget_id);
    else          table->applyLeave("hoverChanged", widget_id);

    if (!table->shouldNotify("hoverChanged", widget_id)) return;

    ElaraOutboundEventQueue::getInstance()->push(
        handle,
        "hoverChanged",
        String("{\"hovered\":") + String(hovered ? "true" : "false") + String("}")
    );
}

void ElaraOutboundEventFilter::onWidgetKeyDown(
    ElaraWidgetHandle handle,
    unsigned int keyval
) {
    queue(handle, "keyDown", String("{\"keyval\":") + String((int)keyval) + String("}"));
}

void ElaraOutboundEventFilter::onWidgetKeyUp(
    ElaraWidgetHandle handle,
    unsigned int keyval
) {
    queue(handle, "keyUp", String("{\"keyval\":") + String((int)keyval) + String("}"));
}

void ElaraOutboundEventFilter::onWidgetKeysTyped(
    ElaraWidgetHandle handle,
    const String& text
) {
    queue(handle, "keysTyped", stringPayload("text", text));
}

void ElaraOutboundEventFilter::onWidgetTextChanged(
    ElaraWidgetHandle handle,
    const String& text
) {
    queue(handle, "textChanged", stringPayload("text", text));
}

void ElaraOutboundEventFilter::onWidgetTextChangedWithCaret(
    ElaraWidgetHandle handle,
    const String& text,
    int caret
) {
    String payload = String("{\"text\":\"") + JsonString::encode(text)
                   + String("\",\"caret\":") + String(caret) + String("}");
    queue(handle, "textChanged", payload);
}

void ElaraOutboundEventFilter::onWidgetValueChanged(
    ElaraWidgetHandle handle,
    double value
) {
    queue(handle, "valueChanged", String("{\"value\":") + String(value) + String("}"));
}

void ElaraOutboundEventFilter::onWidgetAction(
    ElaraWidgetHandle handle,
    const String& action
) {
    queue(handle, "action", stringPayload("action", action));
}

}

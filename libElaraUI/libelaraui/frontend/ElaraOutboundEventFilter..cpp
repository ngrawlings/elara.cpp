#include "ElaraOutboundEventFilter.h"

namespace elara {

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

void ElaraOutboundEventFilter::onWidgetMouseMove(
    ElaraWidgetHandle handle,
    double x,
    double y
) {
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
    queue(
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
    queue(handle, "keysTyped", String("{\"text\":\"") + text + String("\"}"));
}

void ElaraOutboundEventFilter::onWidgetValueChanged(
    ElaraWidgetHandle handle,
    double value
) {
    queue(handle, "valueChanged", String("{\"value\":") + String(value) + String("}"));
}

}

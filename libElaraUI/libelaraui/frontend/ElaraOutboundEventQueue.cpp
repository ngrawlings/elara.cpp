#include "ElaraOutboundEventQueue.h"

namespace elara {

ElaraOutboundEventQueue* ElaraOutboundEventQueue::instance = 0;

ElaraOutboundEvent::ElaraOutboundEvent() {}

ElaraOutboundEvent::ElaraOutboundEvent(
    ElaraWidgetHandle event_widget_handle,
    const String& event_action,
    const String& event_payload
) : widget_handle(event_widget_handle),
    action(event_action),
    payload(event_payload) {}

ElaraOutboundEventQueue::ElaraOutboundEventQueue() {}

ElaraOutboundEventQueue::~ElaraOutboundEventQueue() {}

ElaraOutboundEventQueue* ElaraOutboundEventQueue::getInstance() {
    if(!instance) {
        instance = new ElaraOutboundEventQueue();
    }

    return instance;
}

void ElaraOutboundEventQueue::push(
    ElaraWidgetHandle widget_handle,
    const String& action,
    const String& payload
) {
    Mutex::Lock lock(mutex);
    Memory handle_memory = widget_handle.getHandle();
    String handle_text((const char*)handle_memory.getPtr(), handle_memory.length());

    printf("Event Queued {%s} %s (%s)\n", (const char*)handle_text, (const char*)action, (const char*)payload);

    events.push(
        Ref<ElaraOutboundEvent>(
            new ElaraOutboundEvent(widget_handle, action, payload)
        )
    );
}

bool ElaraOutboundEventQueue::hasEvents() {
    Mutex::Lock lock(mutex);

    return events.length() > 0;
}

Ref<ElaraOutboundEvent> ElaraOutboundEventQueue::pop() {
    Mutex::Lock lock(mutex);

    if(events.length() <= 0) {
        return Ref<ElaraOutboundEvent>(0);
    }

    Ref<ElaraOutboundEvent> event = events[0];
    events.remove(0);

    return event;
}

int ElaraOutboundEventQueue::length() {
    Mutex::Lock lock(mutex);

    return (int)events.length();
}

}

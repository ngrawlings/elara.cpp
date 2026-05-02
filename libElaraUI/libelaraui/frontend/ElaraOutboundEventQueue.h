#ifndef ELARA_OUTBOUND_EVENT_QUEUE_H
#define ELARA_OUTBOUND_EVENT_QUEUE_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <libelarathreads/Mutex.h>

#include "widgets/ElaraWidget.h"

namespace elara {

class ElaraOutboundEvent {
public:
    ElaraWidgetHandle widget_handle;
    String action;
    String payload;

    ElaraOutboundEvent();
    ElaraOutboundEvent(
        ElaraWidgetHandle event_widget_handle,
        const String& event_action,
        const String& event_payload
    );
};

class ElaraOutboundEventQueue {
public:
    virtual ~ElaraOutboundEventQueue();

    static ElaraOutboundEventQueue* getInstance();

    void push(
        ElaraWidgetHandle widget_handle,
        const String& action,
        const String& payload = String()
    );

    bool hasEvents();
    Ref<ElaraOutboundEvent> pop();
    int length();

protected:
    ElaraOutboundEventQueue();

    static ElaraOutboundEventQueue* instance;

    Mutex mutex;
    Array< Ref<ElaraOutboundEvent> > events;
};

}

#endif
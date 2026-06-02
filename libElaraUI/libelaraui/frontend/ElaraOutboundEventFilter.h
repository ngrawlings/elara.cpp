#ifndef ELARA_OUTBOUND_EVENT_FILTER_H
#define ELARA_OUTBOUND_EVENT_FILTER_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/HashMap.h>

#include "listeners/WidgetListener.h"
#include "ElaraOutboundEventQueue.h"

namespace elara {

class ElaraOutboundEventFilter : public WidgetListener {
public:
    ElaraOutboundEventFilter();

    void enable(const String& action);
    void disable(const String& action);
    bool isEnabled(const String& action) const;

    void onWidgetMouseMove(
        ElaraWidgetHandle handle,
        double x,
        double y
    );

    void onWidgetMouseDown(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    );

    void onWidgetMouseUp(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    );

    void onWidgetMouseScroll(
        ElaraWidgetHandle handle,
        double dx,
        double dy
    );

    void onWidgetClicked(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    );

    void onWidgetHoverChanged(
        ElaraWidgetHandle handle,
        bool hovered
    );

    void onWidgetKeyDown(
        ElaraWidgetHandle handle,
        unsigned int keyval
    );

    void onWidgetKeyUp(
        ElaraWidgetHandle handle,
        unsigned int keyval
    );

    void onWidgetKeysTyped(
        ElaraWidgetHandle handle,
        const String& text
    );

    void onWidgetTextChanged(
        ElaraWidgetHandle handle,
        const String& text
    );

    void onWidgetTextChangedWithCaret(
        ElaraWidgetHandle handle,
        const String& text,
        int caret
    );

    void onWidgetValueChanged(
        ElaraWidgetHandle handle,
        double value
    );

    void onWidgetAction(
        ElaraWidgetHandle handle,
        const String& action
    );

private:
    HashMap<String> whitelist;

    void queue(
        ElaraWidgetHandle handle,
        const String& action,
        const String& payload
    );

    String mousePayload(double x, double y) const;
    String buttonPayload(int button, double x, double y) const;
    String scrollPayload(double dx, double dy) const;
    String stringPayload(const String& field, const String& value) const;
};

}

#endif

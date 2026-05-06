#ifndef ELARA_LISTENER_H
#define ELARA_LISTENER_H

#include "../ElaraWidgetHandle.h"

namespace elara {

class WidgetListener {
public:
    virtual ~WidgetListener() {}

    virtual void onWidgetMouseMove(
        ElaraWidgetHandle handle,
        double x,
        double y
    ) {}

    virtual void onWidgetMouseDown(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    ) {}

    virtual void onWidgetMouseUp(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    ) {}

    virtual void onWidgetClicked(
        ElaraWidgetHandle handle,
        int button,
        double x,
        double y
    ) {}

    virtual void onWidgetHoverChanged(
        ElaraWidgetHandle handle,
        bool hovered
    ) {}

    virtual void onWidgetKeyDown(
        ElaraWidgetHandle handle,
        unsigned int keyval
    ) {}

    virtual void onWidgetKeyUp(
        ElaraWidgetHandle handle,
        unsigned int keyval
    ) {}

    virtual void onWidgetKeysTyped(
        ElaraWidgetHandle handle,
        const String& text
    ) {}

    virtual void onWidgetValueChanged(
        ElaraWidgetHandle handle,
        double value
    ) {}

    virtual void onWidgetAction(
        ElaraWidgetHandle handle,
        const String& action
    ) {}

};

}

#endif

#ifndef ELARA_WIDGET_REGISTRY_H
#define ELARA_WIDGET_REGISTRY_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/Ref.h>

#include "widgets/ElaraWidget.h"

namespace elara {

class ElaraWidgetRegistry {
public:
    virtual ~ElaraWidgetRegistry();

    static ElaraWidgetRegistry* getInstance();

    void setWidget(ElaraWidgetHandle widget_handle, ElaraWidget *widget);
    void removeWidget(ElaraWidgetHandle widget_handle);
    Ref<ElaraWidget> getWidget(ElaraWidgetHandle widget_handle) const;

protected:
    ElaraWidgetRegistry();

    static ElaraWidgetRegistry* instance;
    HashMap<ElaraWidget> widgets;
};

}

#endif

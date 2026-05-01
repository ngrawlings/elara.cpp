#include "ElaraWidgetRegistry.h"

namespace elara {

    ElaraWidgetRegistry* ElaraWidgetRegistry::instance = 0;

    ElaraWidgetRegistry::ElaraWidgetRegistry() {

    }

    ElaraWidgetRegistry::~ElaraWidgetRegistry() {

    }

    void ElaraWidgetRegistry::setWidget(ElaraWidgetHandle widget_handle, ElaraWidget *widget) {
        this->widgets.set(widget_handle.getHandle(), Ref<ElaraWidget>((ElaraWidget*)widget));
    }

    Ref<ElaraWidget> ElaraWidgetRegistry::getWidget(ElaraWidgetHandle widget_handle) const {
        Memory mem = widget_handle.getHandle();
        return this->widgets.get(mem);
    }

    ElaraWidgetRegistry* ElaraWidgetRegistry::getInstance() {
        if (!ElaraWidgetRegistry::instance)
            ElaraWidgetRegistry::instance = new ElaraWidgetRegistry();
        return ElaraWidgetRegistry::instance;
    }

}
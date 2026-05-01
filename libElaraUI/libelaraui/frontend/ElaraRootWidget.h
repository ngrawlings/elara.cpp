#ifndef ELARA_ROOT_WIDGET_H
#define ELARA_ROOT_WIDGET_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/HashMap.h>

#include "ElaraWidget.h"
#include "ElaraPopupWidget.h"

namespace elara {

class ElaraRootWidget : public ElaraWidget, public ElaraWidgetRegister {
private:
    ElaraWidgetHandle content;
    ElaraWidgetHandle focus;
    ElaraWidgetHandle popup;

    HashMap<ElaraWidget> widgets;

public:
    ElaraRootWidget();

    void setContent(ElaraWidgetHandle root_content);
    Ref<ElaraWidget> getContent() const;

    void setPopup(ElaraWidgetHandle root_popup);
    Ref<ElaraWidget> getPopup() const;

    void registerWidget(ElaraWidgetHandle widget_handle, void* widget);
    Ref<ElaraWidget> getWidget(ElaraWidgetHandle widget_handle) const;
    void setFocus(ElaraWidgetHandle widget_handle);

    void setPalette(ElaraPalette* widget_palette);

    void draw(ElaraDrawContext* ctx);

    void dispatchMouseMove(double px, double py);
    void dispatchMouseDown(int button, double px, double py);
    void dispatchMouseUp(int button, double px, double py);

    void dispatchKeyDown(unsigned int keyval);
    void dispatchKeyUp(unsigned int keyval);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    void onKeyDown(unsigned int keyval);
    void onKeyUp(unsigned int keyval);
};

}

#endif

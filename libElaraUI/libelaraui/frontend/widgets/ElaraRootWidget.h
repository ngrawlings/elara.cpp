#ifndef ELARA_ROOT_WIDGET_H
#define ELARA_ROOT_WIDGET_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/HashMap.h>

#include "../ElaraWidgetRegistry.h"
#include "../ElaraWidgetStateProbe.h"
#include "ElaraWidget.h"
#include "ElaraPopupWidget.h"
#include "../ElaraOutboundEventFilter.h"

namespace elara {

class ElaraRootWidget : public ElaraWidget, public ElaraWidgetRegister {
private:
    ElaraWidgetHandle content;
    ElaraWidgetHandle focus;
    Array<ElaraWidgetHandle> popups;

    Ref<WidgetListener> event_filter;

public:
    ElaraRootWidget();

    void setContent(ElaraWidgetHandle root_content);
    Ref<ElaraWidget> getContent() const;

    void setPopup(ElaraWidgetHandle root_popup);
    Ref<ElaraWidget> getPopup() const;
    void clearPopups();
    void pushPopup(ElaraWidgetHandle root_popup);
    void removePopup(ElaraWidgetHandle root_popup);
    void dismissAllPopups();
    void dismissPopupsAfter(ElaraWidgetHandle root_popup);
    int popupCount() const;
    Ref<ElaraWidget> getPopup(int index) const;

    void registerWidget(ElaraWidgetHandle widget_handle, void* widget);
    void onWidgetRemoved(ElaraWidgetHandle widget_handle);
    Ref<ElaraWidget> getWidget(ElaraWidgetHandle widget_handle) const;
    bool probeWidgetState(ElaraWidgetHandle widget_handle, ElaraWidgetState& state) const;
    bool probeWidgetSnapshot(ElaraWidgetHandle widget_handle, ElaraWidgetSnapshot& snapshot) const;
    void probeRootSnapshot(ElaraRootSnapshot& snapshot) const;
    String getWidgetStateJson(ElaraWidgetHandle widget_handle) const;
    String getWidgetSnapshotJson(ElaraWidgetHandle widget_handle) const;
    String getRootSnapshotJson() const;
    void setFocus(ElaraWidgetHandle widget_handle);
    ElaraWidgetHandle getFocus() const;
    void enableOutboundEvent(const String& action);
    void disableOutboundEvent(const String& action);

    void setPalette(ElaraPalette* widget_palette);

    void draw(ElaraDrawContext* ctx);
    ElaraMouseCursor currentCursor(double x, double y);

    void dispatchMouseMove(double px, double py);
    void dispatchMouseDown(int button, double px, double py);
    void dispatchMouseUp(int button, double px, double py);

    void dispatchKeyDown(unsigned int keyval);
    void dispatchKeyUp(unsigned int keyval);
    void dispatchKeyDown(unsigned int keyval, unsigned int modifiers);
    void dispatchKeyUp(unsigned int keyval, unsigned int modifiers);

    bool eventPropagate(ElaraUiEvent event);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    void onKeyDown(unsigned int keyval);
    void onKeyUp(unsigned int keyval);
    void onKeyDown(unsigned int keyval, unsigned int modifiers);
    void onKeyUp(unsigned int keyval, unsigned int modifiers);
};

}

#endif

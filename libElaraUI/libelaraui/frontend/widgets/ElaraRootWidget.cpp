#include "ElaraRootWidget.h"
#include "ElaraTextInputWidget.h"
#include "../ElaraWidgetStateProbe.h"

namespace elara {

namespace {

bool handlesEqual(const ElaraWidgetHandle& left, const ElaraWidgetHandle& right) {
    return left.getHandle() == right.getHandle();
}

ElaraPopupWidget* asPopup(Ref<ElaraWidget> widget) {
    return widget ? dynamic_cast<ElaraPopupWidget*>(widget.getPtr()) : 0;
}

}

ElaraRootWidget::ElaraRootWidget() {
    event_filter = Ref<WidgetListener>(new ElaraOutboundEventFilter());
}

void ElaraRootWidget::setContent(ElaraWidgetHandle root_content) {
    content = root_content;

    Ref<ElaraWidget> c = getWidget(content);
    if(c.getPtr()) {
        c->setPalette(palette);
    }
}

Ref<ElaraWidget> ElaraRootWidget::getContent() const {
    return getWidget(content);
}

void ElaraRootWidget::setPopup(ElaraWidgetHandle root_popup) {
    clearPopups();
    pushPopup(root_popup);
}

void ElaraRootWidget::clearPopups() {
    popups.clear();
}

Ref<ElaraWidget> ElaraRootWidget::getPopup() const {
    return getPopup(0);
}

void ElaraRootWidget::pushPopup(ElaraWidgetHandle root_popup) {
    if(root_popup.getHandle().length() <= 0) {
        return;
    }

    removePopup(root_popup);
    popups.push(root_popup);

    Ref<ElaraWidget> popup = getWidget(root_popup);
    if(popup.getPtr()) {
        popup->setPalette(palette);
        popup->setZOrder(1000 + (int)popups.length());
    }
}

void ElaraRootWidget::removePopup(ElaraWidgetHandle root_popup) {
    for(int i = 0; i < (int)popups.length(); i++) {
        if(handlesEqual(popups[i], root_popup)) {
            popups.remove(i);
            return;
        }
    }
}

int ElaraRootWidget::popupCount() const {
    return (int)popups.length();
}

Ref<ElaraWidget> ElaraRootWidget::getPopup(int index) const {
    if(index < 0 || index >= (int)popups.length()) {
        return Ref<ElaraWidget>(0);
    }

    return getWidget(popups[index]);
}

void ElaraRootWidget::registerWidget(ElaraWidgetHandle widget_handle, void* widget) {
    ElaraWidgetRegistry::getInstance()->setWidget(widget_handle, (ElaraWidget*)widget);
    ((ElaraWidget*)widget)->addListener(this->event_filter);
}

void ElaraRootWidget::onWidgetRemoved(ElaraWidgetHandle widget_handle) {
    if(handlesEqual(content, widget_handle)) {
        content = ElaraWidgetHandle();
    }

    if(handlesEqual(focus, widget_handle)) {
        Ref<ElaraWidget> focused_widget = getWidget(focus);
        ElaraTextInputWidget* focused_input = focused_widget
            ? dynamic_cast<ElaraTextInputWidget*>(focused_widget.getPtr())
            : 0;

        if(focused_input) {
            focused_input->setFocused(false);
        }

        focus = ElaraWidgetHandle();
    }

    removePopup(widget_handle);
}

Ref<ElaraWidget> ElaraRootWidget::getWidget(ElaraWidgetHandle widget_handle) const {
    return ElaraWidgetRegistry::getInstance()->getWidget(widget_handle);
}

bool ElaraRootWidget::probeWidgetState(
    ElaraWidgetHandle widget_handle,
    ElaraWidgetState& state
) const {
    Ref<ElaraWidget> widget = getWidget(widget_handle);

    if(!widget) {
        state = ElaraWidgetState();
        return false;
    }

    state = ElaraWidgetStateProbe::widgetState(widget);
    return true;
}

bool ElaraRootWidget::probeWidgetSnapshot(
    ElaraWidgetHandle widget_handle,
    ElaraWidgetSnapshot& snapshot
) const {
    Ref<ElaraWidget> widget = getWidget(widget_handle);

    if(!widget) {
        snapshot = ElaraWidgetSnapshot();
        return false;
    }

    snapshot = ElaraWidgetStateProbe::widgetSnapshot(widget);
    return true;
}

void ElaraRootWidget::probeRootSnapshot(ElaraRootSnapshot& snapshot) const {
    snapshot = ElaraWidgetStateProbe::rootSnapshot((ElaraRootWidget*)this);
}

String ElaraRootWidget::getWidgetStateJson(ElaraWidgetHandle widget_handle) const {
    ElaraWidgetState state;
    if(!probeWidgetState(widget_handle, state)) {
        return "{}";
    }
    return ElaraWidgetStateProbe::widgetStateJson(state);
}

String ElaraRootWidget::getWidgetSnapshotJson(ElaraWidgetHandle widget_handle) const {
    ElaraWidgetSnapshot snapshot;
    if(!probeWidgetSnapshot(widget_handle, snapshot)) {
        return "null";
    }
    return ElaraWidgetStateProbe::widgetSnapshotJson(snapshot);
}

String ElaraRootWidget::getRootSnapshotJson() const {
    ElaraRootSnapshot snapshot;
    probeRootSnapshot(snapshot);
    return ElaraWidgetStateProbe::rootSnapshotJson(snapshot);
}

void ElaraRootWidget::setFocus(ElaraWidgetHandle widget_handle) {
    this->focus = widget_handle;
}

ElaraWidgetHandle ElaraRootWidget::getFocus() const {
    return focus;
}

void ElaraRootWidget::enableOutboundEvent(const String& action) {
    ElaraOutboundEventFilter* filter = (ElaraOutboundEventFilter*)event_filter.getPtr();

    if(filter) {
        filter->enable(action);
    }
}

void ElaraRootWidget::disableOutboundEvent(const String& action) {
    ElaraOutboundEventFilter* filter = (ElaraOutboundEventFilter*)event_filter.getPtr();

    if(filter) {
        filter->disable(action);
    }
}

void ElaraRootWidget::setPalette(ElaraPalette* widget_palette) {
    ElaraWidget::setPalette(widget_palette);

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->setPalette(widget_palette);
    }

    for(int i = 0; i < (int)popups.length(); i++) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        if(popup) {
            popup->setPalette(widget_palette);
        }
    }
}

void ElaraRootWidget::draw(ElaraDrawContext* ctx) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->setBounds(0, 0, width, height);
        c->onDraw(ctx, (int)width, (int)height);
    }

    for(int i = 0; i < (int)popups.length(); i++) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        ElaraPopupWidget* popup_widget = asPopup(popup);

        if(popup_widget && popup_widget->isVisible()) {
            popup->draw(ctx);
        }
    }
}

bool ElaraRootWidget::eventPropagate(ElaraUiEvent event) {
    bool is_mouse =
        event.type == ELARA_UI_MOUSE_MOVE ||
        event.type == ELARA_UI_MOUSE_DOWN ||
        event.type == ELARA_UI_MOUSE_UP;

    if(!is_mouse) {
        return ElaraWidget::eventPropagate(event);
    }

    ElaraPopupWidget* topmost_visible = 0;

    for(int i = (int)popups.length() - 1; i >= 0; i--) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        ElaraPopupWidget* popup_widget = asPopup(popup);

        if(!popup_widget || !popup_widget->isVisible()) {
            continue;
        }

        topmost_visible = popup_widget;

        if(popup_widget->contains(event.x, event.y)) {
            return popup_widget->eventPropagate(event);
        }
    }

    if(topmost_visible) {
        if(event.type == ELARA_UI_MOUSE_DOWN) {
            for(int i = 0; i < (int)popups.length(); i++) {
                ElaraPopupWidget* popup_widget = asPopup(getWidget(popups[i]));
                if(popup_widget && popup_widget->isVisible()) {
                    popup_widget->hide();
                }
            }
        }

        if(event.type == ELARA_UI_MOUSE_DOWN || event.type == ELARA_UI_MOUSE_UP) {
            return true;
        }
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        return c->eventPropagate(event);
    }

    return handleEvent(event);
}

void ElaraRootWidget::dispatchMouseMove(double px, double py) {
    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_MOVE;
    event.x = px;
    event.y = py;
    event.button = 0;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchMouseDown(int button, double px, double py) {
    Ref<ElaraWidget> focused_widget = getWidget(focus);
    ElaraTextInputWidget* focused_input = focused_widget
        ? dynamic_cast<ElaraTextInputWidget*>(focused_widget.getPtr())
        : 0;

    if(focused_input) {
        bool inside_focused =
            px >= focused_input->getAbsoluteX() &&
            py >= focused_input->getAbsoluteY() &&
            px <= focused_input->getAbsoluteX() + focused_input->getWidth() &&
            py <= focused_input->getAbsoluteY() + focused_input->getHeight();

        if(!inside_focused) {
            focused_input->setFocused(false);
            focus = ElaraWidgetHandle();
        }
    }

    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_DOWN;
    event.x = px;
    event.y = py;
    event.button = button;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchMouseUp(int button, double px, double py) {
    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_UP;
    event.x = px;
    event.y = py;
    event.button = button;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchKeyDown(unsigned int keyval) {
    Ref<ElaraWidget> focused_widget = getWidget(focus);

    if(focused_widget && focused_widget->isVisible()) {
        focused_widget->onKeyDown(keyval);
        return;
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyDown(keyval);
    }
}

void ElaraRootWidget::dispatchKeyUp(unsigned int keyval) {
    Ref<ElaraWidget> focused_widget = getWidget(focus);

    if(focused_widget && focused_widget->isVisible()) {
        focused_widget->onKeyUp(keyval);
        return;
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyUp(keyval);
    }
}

void ElaraRootWidget::onMouseMove(double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseMove(px, py);
    }
}

void ElaraRootWidget::onMouseDown(int button, double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseDown(button, px, py);
    }
}

void ElaraRootWidget::onMouseUp(int button, double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseUp(button, px, py);
    }
}

void ElaraRootWidget::onKeyDown(unsigned int keyval) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyDown(keyval);
    }
}

void ElaraRootWidget::onKeyUp(unsigned int keyval) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyUp(keyval);
    }
}

}

#include "ElaraRootWidget.h"
#include "ElaraTextInputWidget.h"

namespace elara {

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
    popup = root_popup;

    Ref<ElaraWidget> p = getWidget(popup);
    if(p.getPtr()) {
        p->setPalette(palette);
    }
}

Ref<ElaraWidget> ElaraRootWidget::getPopup() const {
    return getWidget(popup);
}

void ElaraRootWidget::registerWidget(ElaraWidgetHandle widget_handle, void* widget) {
    ElaraWidgetRegistry::getInstance()->setWidget(widget_handle, (ElaraWidget*)widget);
    ((ElaraWidget*)widget)->addListener(this->event_filter);
}

Ref<ElaraWidget> ElaraRootWidget::getWidget(ElaraWidgetHandle widget_handle) const {
    return ElaraWidgetRegistry::getInstance()->getWidget(widget_handle);
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

    Ref<ElaraWidget> p = getWidget(popup);
    if(p) {
        p->setPalette(widget_palette);
    }
}

void ElaraRootWidget::draw(ElaraDrawContext* ctx) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->setBounds(0, 0, width, height);
        c->onDraw(ctx, (int)width, (int)height);
    }

    Ref<ElaraWidget> p = getWidget(popup);
    if(p && ((ElaraPopupWidget*)p.getPtr())->isVisible()) {
        p->draw(ctx);
    }
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

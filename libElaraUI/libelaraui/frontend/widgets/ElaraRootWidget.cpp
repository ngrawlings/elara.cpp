#include "ElaraRootWidget.h"

namespace elara {

ElaraRootWidget::ElaraRootWidget() {}

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
}

Ref<ElaraWidget> ElaraRootWidget::getWidget(ElaraWidgetHandle widget_handle) const {
    return ElaraWidgetRegistry::getInstance()->getWidget(widget_handle);
}

void ElaraRootWidget::setFocus(ElaraWidgetHandle widget_handle) {
    this->focus = widget_handle;
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
    event.button = NULL;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchMouseDown(int button, double px, double py) {
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

}

void ElaraRootWidget::dispatchKeyUp(unsigned int keyval) {

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

#include "ElaraRootWidget.h"

namespace elara {

ElaraRootWidget::ElaraRootWidget() {}

void ElaraRootWidget::setContent(Ref<ElaraWidget> root_content) {
    content = root_content;

    if(content) {
        content->setPalette(palette);
    }
}

Ref<ElaraWidget> ElaraRootWidget::getContent() const {
    return content;
}

void ElaraRootWidget::setPopup(Ref<ElaraPopupWidget> root_popup) {
    popup = root_popup;

    if(popup) {
        popup->setPalette(palette);
    }
}

Ref<ElaraPopupWidget> ElaraRootWidget::getPopup() const {
    return popup;
}

void ElaraRootWidget::setPalette(ElaraPalette* widget_palette) {
    ElaraWidget::setPalette(widget_palette);

    if(content) {
        content->setPalette(widget_palette);
    }

    if(popup) {
        popup->setPalette(widget_palette);
    }
}

void ElaraRootWidget::draw(ElaraDrawContext* ctx) {
    if(content) {
        content->setBounds(0, 0, width, height);
        content->onDraw(ctx, (int)width, (int)height);
    }

    if(popup && popup->isVisible()) {
        popup->draw(ctx);
    }
}

void ElaraRootWidget::onMouseMove(double px, double py) {
    if(popup && popup->isVisible()) {
        popup->onMouseMove(px, py);
        return;
    }

    if(content) {
        content->onMouseMove(px, py);
    }
}

void ElaraRootWidget::onMouseDown(int button, double px, double py) {
    if(popup && popup->isVisible()) {
        popup->onMouseDown(button, px, py);
        return;
    }

    printf("button: %d\n", button);
    if(button == 3 && popup) {
        popup->showAt(px, py);
        return;
    }

    if(content) {
        content->onMouseDown(button, px, py);
    }
}

void ElaraRootWidget::onMouseUp(int button, double px, double py) {
    if(popup && popup->isVisible()) {
        popup->onMouseUp(button, px, py);
        return;
    }

    if(content) {
        content->onMouseUp(button, px, py);
    }
}

void ElaraRootWidget::onKeyDown(unsigned int keyval) {
    if(content) {
        content->onKeyDown(keyval);
    }
}

void ElaraRootWidget::onKeyUp(unsigned int keyval) {
    if(content) {
        content->onKeyUp(keyval);
    }
}

}

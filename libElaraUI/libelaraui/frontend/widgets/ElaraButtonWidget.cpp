#include "ElaraButtonWidget.h"

namespace elara {

ElaraButtonWidget::ElaraButtonWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    text("Button"),
    action(""),
    palette_master("button"),
    pressed(false),
    hovered(false),
    enabled(true),
    font_size(14),
    padding_x(10),
    padding_y(6) {}

ElaraButtonWidget::~ElaraButtonWidget() {}

void ElaraButtonWidget::setText(const String& button_text) {
    text = button_text;
}

String ElaraButtonWidget::getText() const {
    return text;
}

void ElaraButtonWidget::setAction(const String& action_name) {
    action = action_name;
}

String ElaraButtonWidget::getAction() const {
    return action;
}

void ElaraButtonWidget::setEnabled(bool value) {
    enabled = value;

    if(!enabled) {
        pressed = false;
        hovered = false;
    }
}

bool ElaraButtonWidget::isEnabled() const {
    return enabled;
}

void ElaraButtonWidget::setFontSize(double size) {
    font_size = size;
}

void ElaraButtonWidget::setPadding(double px, double py) {
    padding_x = px;
    padding_y = py;
}

double ElaraButtonWidget::estimateTextWidth() const {
    return text.length() * font_size * 0.58;
}

double ElaraButtonWidget::textX() const {
    return (width - estimateTextWidth()) / 2;
}

double ElaraButtonWidget::textY() const {
    return (height / 2) + (font_size / 2) - 2;
}

void ElaraButtonWidget::onClicked() {
    printf("button clicked: %s action=%s\n", (const char*)text, (const char*)action);

    /*
        Later:
        rootWidget()->emitAction(getHandle(), action);
    */
}

void ElaraButtonWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = String("disabled");
    } else if(pressed) {
        sub = String("pressed");
    } else if(hovered) {
        sub = String("hover");
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(textX(), textY(), text, font_size);
}

void ElaraButtonWidget::onMouseMove(double px, double py) {
    bool was_hovered = hovered;
    hovered = containsLocal(px, py);

    emitMouseMove(px, py);

    if(was_hovered != hovered) {
        emitHoverChanged(hovered);
    }

    if(!hovered) {
        pressed = false;
    }
}

void ElaraButtonWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(!enabled || button != 1) {
        return;
    }

    if(containsLocal(px, py)) {
        pressed = true;
    }
}

void ElaraButtonWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(!enabled || button != 1) {
        pressed = false;
        return;
    }

    bool was_pressed = pressed;
    pressed = false;

    if(was_pressed && containsLocal(px, py)) {
        emitClicked(button, px, py);
        onClicked();
    }
}

bool ElaraButtonWidget::wantsFocus() const {
    return enabled;
}

}

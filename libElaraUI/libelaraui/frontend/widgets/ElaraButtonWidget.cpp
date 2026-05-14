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
    padding_left(10),
    padding_right(10),
    padding_top(6),
    padding_bottom(6) {}

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

void ElaraButtonWidget::setPadding(double all) {
    padding_left = padding_right = padding_top = padding_bottom = all;
}

void ElaraButtonWidget::setPadding(double px, double py) {
    padding_left = padding_right = px;
    padding_top = padding_bottom = py;
}

void ElaraButtonWidget::setPaddingLeft(double value)   { padding_left   = value; }
void ElaraButtonWidget::setPaddingRight(double value)  { padding_right  = value; }
void ElaraButtonWidget::setPaddingTop(double value)    { padding_top    = value; }
void ElaraButtonWidget::setPaddingBottom(double value) { padding_bottom = value; }

void ElaraButtonWidget::onClicked() {
    if(action.length() > 0) {
        emitAction(action);
    }
}

void ElaraButtonWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");
    double content_offset = 0;

    if(!enabled) {
        sub = String("disabled");
    } else if(pressed) {
        sub = String("pressed");
        content_offset = 1;
    } else if(hovered) {
        sub = String("hover");
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);
    double r = c.corner_radius;
    double bw = c.border_width;

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRoundRect(0, 0, width, height, r);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->strokeRoundRect(0, 0, width, height, r, bw);

    if(hovered && enabled && !pressed) {
        double inset = bw + 1.0;
        ctx->fillRoundRect(inset, height - 4.0 - inset, width - inset * 2.0, 3.0, r > 0 ? r - 1.0 : 0.0);
    }

    double text_w = ctx->measureTextWidth(text, font_size);
    double text_x = (width - text_w) / 2.0;
    if(text_x < padding_left) text_x = padding_left;
    if(text_x + text_w > width - padding_right) text_x = width - padding_right - text_w;

    double text_y = (height / 2.0) + (font_size / 2.0) - 2.0;
    if(text_y - font_size < padding_top) text_y = padding_top + font_size;
    if(text_y > height - padding_bottom) text_y = height - padding_bottom;

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(text_x + content_offset, text_y + content_offset, text, font_size);
}

ElaraMouseCursor ElaraButtonWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
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

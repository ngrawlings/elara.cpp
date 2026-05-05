#include "ElaraCheckboxWidget.h"

namespace elara {

ElaraCheckboxWidget::ElaraCheckboxWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    text("Checkbox"),
    palette_master("button"),
    checked(false),
    enabled(true),
    hovered(false),
    pressed(false),
    font_size(14),
    box_size(18),
    gap(10) {}

ElaraCheckboxWidget::~ElaraCheckboxWidget() {}

void ElaraCheckboxWidget::setText(const String& checkbox_text) {
    text = checkbox_text;
}

String ElaraCheckboxWidget::getText() const {
    return text;
}

void ElaraCheckboxWidget::setChecked(bool value) {
    checked = value;
}

bool ElaraCheckboxWidget::isChecked() const {
    return checked;
}

void ElaraCheckboxWidget::setEnabled(bool value) {
    enabled = value;

    if(!enabled) {
        hovered = false;
        pressed = false;
    }
}

bool ElaraCheckboxWidget::isEnabled() const {
    return enabled;
}

void ElaraCheckboxWidget::setFontSize(double size) {
    font_size = size;
}

double ElaraCheckboxWidget::getFontSize() const {
    return font_size;
}

double ElaraCheckboxWidget::textY() const {
    return (height / 2) + (font_size / 2) - 2;
}

void ElaraCheckboxWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = String("disabled");
    } else if(pressed) {
        sub = String("pressed");
    } else if(hovered) {
        sub = String("hover");
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);
    double box_y = (height - box_size) / 2.0;

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, box_y, box_size, box_size);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, box_y, box_size, box_y, 1);
    ctx->line(0, box_y + box_size, box_size, box_y + box_size, 1);
    ctx->line(0, box_y, 0, box_y + box_size, 1);
    ctx->line(box_size, box_y, box_size, box_y + box_size, 1);

    if(checked) {
        ctx->fillRect(4, box_y + 4, box_size - 8, box_size - 8);
        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->line(5, box_y + (box_size / 2), 8, box_y + box_size - 6, 2);
        ctx->line(8, box_y + box_size - 6, box_size - 4, box_y + 5, 2);
    } else if(hovered && enabled) {
        ctx->fillRect(3, box_y + box_size + 3, box_size - 6, 2);
    }

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(box_size + gap, textY(), text, font_size);
}

void ElaraCheckboxWidget::onMouseMove(double px, double py) {
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

void ElaraCheckboxWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(!enabled || button != 1) {
        return;
    }

    if(containsLocal(px, py)) {
        pressed = true;
    }
}

void ElaraCheckboxWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(!enabled || button != 1) {
        pressed = false;
        return;
    }

    bool was_pressed = pressed;
    pressed = false;

    if(was_pressed && containsLocal(px, py)) {
        checked = !checked;
        emitValueChanged(checked ? 1.0 : 0.0);
        emitClicked(button, px, py);
    }
}

}

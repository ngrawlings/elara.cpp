#include "ElaraRadioButtonWidget.h"

namespace elara {

ElaraRadioButtonWidget::ElaraRadioButtonWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    text("Radio"),
    group("default"),
    palette_master("button"),
    checked(false),
    enabled(true),
    hovered(false),
    pressed(false),
    font_size(14),
    circle_size(18),
    gap(10) {
}

ElaraRadioButtonWidget::~ElaraRadioButtonWidget() {
}

void ElaraRadioButtonWidget::setText(const String& radio_text) {
    text = radio_text;
}

String ElaraRadioButtonWidget::getText() const {
    return text;
}

void ElaraRadioButtonWidget::setGroup(const String& group_name) {
    group = group_name.length() > 0 ? group_name : String("default");
}

String ElaraRadioButtonWidget::getGroup() const {
    return group;
}

void ElaraRadioButtonWidget::setChecked(bool value) {
    checked = value;
}

bool ElaraRadioButtonWidget::isChecked() const {
    return checked;
}

void ElaraRadioButtonWidget::setEnabled(bool value) {
    enabled = value;

    if(!enabled) {
        hovered = false;
        pressed = false;
    }
}

bool ElaraRadioButtonWidget::isEnabled() const {
    return enabled;
}

void ElaraRadioButtonWidget::setFontSize(double size) {
    font_size = size;
}

double ElaraRadioButtonWidget::getFontSize() const {
    return font_size;
}

double ElaraRadioButtonWidget::textY() const {
    return (height / 2) + (font_size / 2) - 2;
}

void ElaraRadioButtonWidget::uncheckSiblingRadios() {
    ElaraWidget* parent_widget = getParent();

    if(!parent_widget) {
        return;
    }

    for(int i = 0; i < parent_widget->childCount(); i++) {
        Ref<ElaraWidget> child = parent_widget->getChild(i);
        ElaraRadioButtonWidget* radio = dynamic_cast<ElaraRadioButtonWidget*>(child.getPtr());

        if(!radio || radio == this) {
            continue;
        }

        if(radio->getGroup() == group) {
            radio->setChecked(false);
        }
    }
}

void ElaraRadioButtonWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = String("disabled");
    } else if(pressed) {
        sub = String("pressed");
    } else if(hovered) {
        sub = String("hover");
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);
    double radius = circle_size / 2.0;
    double center_x = radius;
    double center_y = height / 2.0;

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillCircle(center_x, center_y, radius);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(center_x - radius, center_y, center_x + radius, center_y, 1);
    ctx->line(center_x, center_y - radius, center_x, center_y + radius, 1);

    if(checked) {
        ctx->fillCircle(center_x, center_y, radius - 5);
    } else if(hovered && enabled) {
        ctx->fillRect(3, center_y + radius + 2, circle_size - 6, 2);
    }

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(circle_size + gap, textY(), text, font_size);
}

void ElaraRadioButtonWidget::onMouseMove(double px, double py) {
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

void ElaraRadioButtonWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(!enabled || button != 1) {
        return;
    }

    if(containsLocal(px, py)) {
        pressed = true;
    }
}

void ElaraRadioButtonWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(!enabled || button != 1) {
        pressed = false;
        return;
    }

    bool was_pressed = pressed;
    pressed = false;

    if(was_pressed && containsLocal(px, py)) {
        uncheckSiblingRadios();
        checked = true;
        emitValueChanged(1.0);
        emitClicked(button, px, py);
    }
}

}

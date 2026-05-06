#include "ElaraSpinnerWidget.h"

namespace elara {

ElaraSpinnerWidget::ElaraSpinnerWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    palette_master("input"),
    minimum(0),
    maximum(100),
    value(0),
    step(1),
    font_size(14),
    button_width(24),
    enabled(true),
    hovered(false),
    pressing_up(false),
    pressing_down(false) {
}

ElaraSpinnerWidget::~ElaraSpinnerWidget() {
}

double ElaraSpinnerWidget::clampValue(double candidate) const {
    if(candidate < minimum) {
        return minimum;
    }

    if(candidate > maximum) {
        return maximum;
    }

    return candidate;
}

double ElaraSpinnerWidget::quantizeValue(double candidate) const {
    candidate = clampValue(candidate);

    if(step <= 0) {
        return candidate;
    }

    double stepped = ((long long)(((candidate - minimum) / step) + 0.5)) * step;
    return clampValue(minimum + stepped);
}

bool ElaraSpinnerWidget::inUpButton(double px, double py) const {
    return px >= width - button_width && py >= 0 && py < (height / 2.0);
}

bool ElaraSpinnerWidget::inDownButton(double px, double py) const {
    return px >= width - button_width && py >= (height / 2.0) && py <= height;
}

double ElaraSpinnerWidget::textY() const {
    return (height / 2.0) + (font_size / 2.0) - 2;
}

String ElaraSpinnerWidget::valueText() const {
    return String(value);
}

void ElaraSpinnerWidget::applyValue(double candidate, bool emit_change) {
    double updated = quantizeValue(candidate);

    if(updated == value) {
        return;
    }

    value = updated;

    if(emit_change) {
        emitValueChanged();
    }
}

void ElaraSpinnerWidget::emitValueChanged() {
    ElaraWidget::emitValueChanged(value);
}

void ElaraSpinnerWidget::setRange(double min_value, double max_value) {
    minimum = min_value;
    maximum = max_value;

    if(maximum < minimum) {
        double swap = minimum;
        minimum = maximum;
        maximum = swap;
    }

    value = clampValue(value);
}

double ElaraSpinnerWidget::getMinimum() const {
    return minimum;
}

double ElaraSpinnerWidget::getMaximum() const {
    return maximum;
}

void ElaraSpinnerWidget::setValue(double current_value) {
    value = quantizeValue(current_value);
}

double ElaraSpinnerWidget::getValue() const {
    return value;
}

void ElaraSpinnerWidget::setStep(double step_value) {
    step = step_value > 0 ? step_value : 0;
    value = quantizeValue(value);
}

double ElaraSpinnerWidget::getStep() const {
    return step;
}

void ElaraSpinnerWidget::setEnabled(bool value_state) {
    enabled = value_state;

    if(!enabled) {
        hovered = false;
        pressing_up = false;
        pressing_down = false;
    }
}

bool ElaraSpinnerWidget::isEnabled() const {
    return enabled;
}

void ElaraSpinnerWidget::setFontSize(double size) {
    font_size = size;
}

double ElaraSpinnerWidget::getFontSize() const {
    return font_size;
}

ElaraMouseCursor ElaraSpinnerWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraSpinnerWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = "disabled";
    } else if(pressing_up || pressing_down) {
        sub = "pressed";
    } else if(hovered) {
        sub = "hover";
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);
    ElaraPaletteTriplet button = colors("button", sub);
    double split_y = height / 2.0;
    double field_width = width - button_width;

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);
    ctx->line(field_width, 0, field_width, height, 1);
    ctx->line(field_width, split_y, width, split_y, 1);

    ctx->setColor(button.base.r, button.base.g, button.base.b);
    ctx->fillRect(field_width, 0, button_width, split_y);
    ctx->fillRect(field_width, split_y, button_width, height - split_y);

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(8, textY(), valueText(), font_size);

    double up_center_x = field_width + (button_width / 2.0);
    double up_center_y = split_y / 2.0;
    ctx->line(up_center_x - 5, up_center_y + 3, up_center_x, up_center_y - 3, 1);
    ctx->line(up_center_x, up_center_y - 3, up_center_x + 5, up_center_y + 3, 1);

    double down_center_x = up_center_x;
    double down_center_y = split_y + ((height - split_y) / 2.0);
    ctx->line(down_center_x - 5, down_center_y - 3, down_center_x, down_center_y + 3, 1);
    ctx->line(down_center_x, down_center_y + 3, down_center_x + 5, down_center_y - 3, 1);
}

void ElaraSpinnerWidget::onMouseMove(double px, double py) {
    bool was_hovered = hovered;
    hovered = containsLocal(px, py);

    emitMouseMove(px, py);

    if(was_hovered != hovered) {
        emitHoverChanged(hovered);
    }

    if(!hovered) {
        pressing_up = false;
        pressing_down = false;
    }
}

void ElaraSpinnerWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(!enabled || button != 1 || !containsLocal(px, py)) {
        return;
    }

    pressing_up = inUpButton(px, py);
    pressing_down = inDownButton(px, py);
}

void ElaraSpinnerWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(button != 1) {
        pressing_up = false;
        pressing_down = false;
        return;
    }

    bool was_pressing_up = pressing_up;
    bool was_pressing_down = pressing_down;
    pressing_up = false;
    pressing_down = false;

    if(!enabled || !containsLocal(px, py)) {
        return;
    }

    if(was_pressing_up && inUpButton(px, py)) {
        applyValue(value + step, true);
        emitClicked(button, px, py);
        return;
    }

    if(was_pressing_down && inDownButton(px, py)) {
        applyValue(value - step, true);
        emitClicked(button, px, py);
    }
}

}

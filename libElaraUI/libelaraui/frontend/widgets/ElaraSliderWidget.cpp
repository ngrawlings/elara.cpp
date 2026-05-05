#include "ElaraSliderWidget.h"

namespace elara {

ElaraSliderWidget::ElaraSliderWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    orientation("horizontal"),
    palette_master("input"),
    minimum(0),
    maximum(100),
    value(0),
    step(1),
    knob_size(18),
    enabled(true),
    hovered(false),
    dragging(false) {
}

ElaraSliderWidget::~ElaraSliderWidget() {
}

double ElaraSliderWidget::clampValue(double candidate) const {
    if(candidate < minimum) {
        return minimum;
    }

    if(candidate > maximum) {
        return maximum;
    }

    return candidate;
}

double ElaraSliderWidget::normalizeValue(double candidate) const {
    double range = maximum - minimum;

    if(range <= 0) {
        return 0;
    }

    return (clampValue(candidate) - minimum) / range;
}

double ElaraSliderWidget::quantizeValue(double candidate) const {
    candidate = clampValue(candidate);

    if(step <= 0) {
        return candidate;
    }

    double stepped = ((long long)(((candidate - minimum) / step) + 0.5)) * step;
    return clampValue(minimum + stepped);
}

double ElaraSliderWidget::trackStart() const {
    return knob_size / 2.0;
}

double ElaraSliderWidget::trackLength() const {
    double axis_extent = isVertical() ? height : width;
    double length = axis_extent - knob_size;
    return length > 1 ? length : 1;
}

double ElaraSliderWidget::knobOffset() const {
    return trackStart() + (normalizeValue(value) * trackLength());
}

double ElaraSliderWidget::valueAtPosition(double px, double py) const {
    double axis_position = isVertical() ? py : px;
    double relative = axis_position - trackStart();
    double t = relative / trackLength();

    if(t < 0) {
        t = 0;
    }

    if(t > 1) {
        t = 1;
    }

    return minimum + ((maximum - minimum) * t);
}

bool ElaraSliderWidget::isVertical() const {
    return orientation == String("vertical");
}

bool ElaraSliderWidget::isHorizontal() const {
    return !isVertical();
}

void ElaraSliderWidget::applyValue(double candidate, bool emit_change) {
    double updated = quantizeValue(candidate);

    if(updated == value) {
        return;
    }

    value = updated;

    if(emit_change) {
        emitValueChanged();
    }
}

void ElaraSliderWidget::emitValueChanged() {
    ElaraWidget::emitValueChanged(value);
}

void ElaraSliderWidget::setOrientation(const String& value) {
    if(value == String("vertical")) {
        orientation = "vertical";
        return;
    }

    orientation = "horizontal";
}

String ElaraSliderWidget::getOrientation() const {
    return orientation;
}

void ElaraSliderWidget::setRange(double min_value, double max_value) {
    minimum = min_value;
    maximum = max_value;

    if(maximum < minimum) {
        double swap = minimum;
        minimum = maximum;
        maximum = swap;
    }

    value = clampValue(value);
}

double ElaraSliderWidget::getMinimum() const {
    return minimum;
}

double ElaraSliderWidget::getMaximum() const {
    return maximum;
}

void ElaraSliderWidget::setValue(double current_value) {
    value = quantizeValue(current_value);
}

double ElaraSliderWidget::getValue() const {
    return value;
}

void ElaraSliderWidget::setStep(double step_value) {
    step = step_value > 0 ? step_value : 0;
    value = quantizeValue(value);
}

double ElaraSliderWidget::getStep() const {
    return step;
}

void ElaraSliderWidget::setEnabled(bool slider_enabled) {
    enabled = slider_enabled;

    if(!enabled) {
        dragging = false;
        hovered = false;
    }
}

bool ElaraSliderWidget::isEnabled() const {
    return enabled;
}

void ElaraSliderWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = "disabled";
    } else if(dragging) {
        sub = "pressed";
    } else if(hovered) {
        sub = "hover";
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);
    ElaraPaletteTriplet button = colors("button", sub);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    double start = trackStart();
    double length = trackLength();
    double offset = knobOffset();

    ctx->setColor(c.accent.r * 0.9, c.accent.g * 0.9, c.accent.b * 0.9);
    if(isVertical()) {
        double cx = width / 2.0;
        ctx->line(cx, start, cx, start + length, 3);
        ctx->setColor(button.base.r, button.base.g, button.base.b);
        ctx->fillRect(3, offset - (knob_size / 2.0), width - 6, knob_size);
        ctx->setColor(button.accent.r, button.accent.g, button.accent.b);
        ctx->line(3, offset - (knob_size / 2.0), width - 3, offset - (knob_size / 2.0), 1);
        ctx->line(3, offset + (knob_size / 2.0), width - 3, offset + (knob_size / 2.0), 1);
    } else {
        double cy = height / 2.0;
        ctx->line(start, cy, start + length, cy, 3);
        ctx->setColor(button.base.r, button.base.g, button.base.b);
        ctx->fillRect(offset - (knob_size / 2.0), 3, knob_size, height - 6);
        ctx->setColor(button.accent.r, button.accent.g, button.accent.b);
        ctx->line(offset - (knob_size / 2.0), 3, offset - (knob_size / 2.0), height - 3, 1);
        ctx->line(offset + (knob_size / 2.0), 3, offset + (knob_size / 2.0), height - 3, 1);
    }
}

void ElaraSliderWidget::onMouseMove(double px, double py) {
    bool was_hovered = hovered;
    hovered = containsLocal(px, py);
    emitMouseMove(px, py);

    if(was_hovered != hovered) {
        emitHoverChanged(hovered);
    }

    if(enabled && dragging) {
        applyValue(valueAtPosition(px, py), true);
    }
}

void ElaraSliderWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(!enabled || button != 1 || !containsLocal(px, py)) {
        return;
    }

    dragging = true;
    applyValue(valueAtPosition(px, py), true);
}

void ElaraSliderWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(button != 1) {
        dragging = false;
        return;
    }

    bool was_dragging = dragging;
    dragging = false;

    if(enabled && was_dragging && containsLocal(px, py)) {
        emitClicked(button, px, py);
    }
}

}

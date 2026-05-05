#ifndef ELARA_SLIDER_WIDGET_H
#define ELARA_SLIDER_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraSliderWidget : public ElaraWidget {
private:
    String orientation;
    String palette_master;
    double minimum;
    double maximum;
    double value;
    double step;
    double knob_size;
    bool enabled;
    bool hovered;
    bool dragging;

    double clampValue(double candidate) const;
    double normalizeValue(double candidate) const;
    double quantizeValue(double candidate) const;
    double trackStart() const;
    double trackLength() const;
    double knobOffset() const;
    double valueAtPosition(double px, double py) const;
    bool isVertical() const;
    bool isHorizontal() const;
    void applyValue(double candidate, bool emit_change);
    void emitValueChanged();

public:
    ElaraSliderWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraSliderWidget();

    void setOrientation(const String& value);
    String getOrientation() const;

    void setRange(double min_value, double max_value);
    double getMinimum() const;
    double getMaximum() const;

    void setValue(double current_value);
    double getValue() const;

    void setStep(double step_value);
    double getStep() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void draw(ElaraDrawContext* ctx);
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

#ifndef ELARA_SPINNER_WIDGET_H
#define ELARA_SPINNER_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraSpinnerWidget : public ElaraWidget {
private:
    String palette_master;
    double minimum;
    double maximum;
    double value;
    double step;
    double font_size;
    double button_width;
    bool enabled;
    bool hovered;
    bool pressing_up;
    bool pressing_down;

    double clampValue(double candidate) const;
    double quantizeValue(double candidate) const;
    bool inUpButton(double px, double py) const;
    bool inDownButton(double px, double py) const;
    double textY() const;
    String valueText() const;
    void applyValue(double candidate, bool emit_change);
    void emitValueChanged();

public:
    ElaraSpinnerWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraSpinnerWidget();

    void setRange(double min_value, double max_value);
    double getMinimum() const;
    double getMaximum() const;

    void setValue(double current_value);
    double getValue() const;

    void setStep(double step_value);
    double getStep() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double size);
    double getFontSize() const;

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

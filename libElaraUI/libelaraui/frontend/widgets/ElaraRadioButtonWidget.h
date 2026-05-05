#ifndef ELARA_RADIO_BUTTON_WIDGET_H
#define ELARA_RADIO_BUTTON_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraRadioButtonWidget : public ElaraWidget {
private:
    String text;
    String group;
    String palette_master;

    bool checked;
    bool enabled;
    bool hovered;
    bool pressed;

    double font_size;
    double circle_size;
    double gap;

    double textY() const;
    void uncheckSiblingRadios();

public:
    ElaraRadioButtonWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraRadioButtonWidget();

    void setText(const String& radio_text);
    String getText() const;

    void setGroup(const String& group_name);
    String getGroup() const;

    void setChecked(bool value);
    bool isChecked() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double size);
    double getFontSize() const;

    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

#ifndef ELARA_CHECKBOX_WIDGET_H
#define ELARA_CHECKBOX_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraCheckboxWidget : public ElaraWidget {
private:
    String text;
    String palette_master;

    bool checked;
    bool enabled;
    bool hovered;
    bool pressed;

    double font_size;
    double box_size;
    double gap;

    double textY() const;

public:
    ElaraCheckboxWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraCheckboxWidget();

    void setText(const String& checkbox_text);
    String getText() const;

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

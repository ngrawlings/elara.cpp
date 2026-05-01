#ifndef ELARA_BUTTON_WIDGET_H
#define ELARA_BUTTON_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraButtonWidget : public ElaraWidget {
private:
    String text;
    String action;

    String palette_master;

    bool pressed;
    bool hovered;
    bool enabled;

    double font_size;
    double padding_x;
    double padding_y;

    double estimateTextWidth() const;
    double textX() const;
    double textY() const;

public:
    ElaraButtonWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraButtonWidget();

    void setText(const String& button_text);
    String getText() const;

    void setAction(const String& action_name);
    String getAction() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double size);
    void setPadding(double px, double py);

    virtual void onClicked();

    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    bool wantsFocus() const;
};

}

#endif

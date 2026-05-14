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
    double padding_left;
    double padding_right;
    double padding_top;
    double padding_bottom;

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
    void setPadding(double all);
    void setPadding(double px, double py);
    void setPaddingLeft(double value);
    void setPaddingRight(double value);
    void setPaddingTop(double value);
    void setPaddingBottom(double value);

    virtual void onClicked();

    void draw(ElaraDrawContext* ctx);
    ElaraMouseCursor cursor() const;

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    bool wantsFocus() const;
};

}

#endif

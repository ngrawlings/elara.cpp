#ifndef ELARA_TEXT_INPUT_WIDGET_H
#define ELARA_TEXT_INPUT_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraTextInputWidget : public ElaraWidget {
private:
    String value;
    String placeholder;

    String palette_master;

    bool enabled;
    bool focused;
    bool key_down;
    unsigned int last_keyval;

    double font_size;
    double padding_x;
    double caret_padding;

    int caret_index;

    int visibleTextStart() const;
    String visibleText() const;
    double textViewportWidth() const;

    double estimateTextWidth(const String& text) const;
    double textY() const;
    int caretIndexAtX(double px) const;

    bool isPrintableKey(unsigned int keyval) const;
    char printableChar(unsigned int keyval) const;

    void clampCaret();
    void insertChar(char c);
    void backspace();
    void deleteForward();

public:
    ElaraTextInputWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraTextInputWidget();

    void setText(const String& text_value);
    String getText() const;

    void setPlaceholder(const String& text_value);
    String getPlaceholder() const;

    void setEnabled(bool value);
    bool isEnabled() const;
    void setFocused(bool value);
    bool isFocused() const;

    void setFontSize(double size);
    void setTextPadding(double px);

    virtual void draw(ElaraDrawContext* ctx);

    virtual void onMouseDown(int button, double px, double py);
    virtual void onMouseUp(int button, double px, double py);
    virtual void onKeyDown(unsigned int keyval);
    virtual void onKeyUp(unsigned int keyval);
};

}

#endif

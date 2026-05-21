#ifndef ELARA_TEXT_INPUT_WIDGET_H
#define ELARA_TEXT_INPUT_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraTextInputWidget : public ElaraWidget {
private:
    struct ElaraTextInputMetrics {
        int visible_start;
        String visible_text;
        Array<double> caret_positions;

        ElaraTextInputMetrics()
            : visible_start(0),
              visible_text(""),
              caret_positions() {}
    };

    String value;
    String placeholder;

    String palette_master;

    bool enabled;
    bool focused;
    bool key_down;
    unsigned int last_keyval;
    bool selecting;

    double font_size;
    double padding_x;
    double caret_padding;

    int caret_index;
    int selection_anchor;
    int selection_focus;
    ElaraTextInputMetrics metrics_cache;

    double textViewportWidth() const;

    double estimateTextWidth(const String& text) const;
    double textY() const;
    int caretIndexAtX(double px) const;
    void rebuildMetrics(ElaraDrawContext* ctx);
    double measuredTextWidth(ElaraDrawContext* ctx, const String& text) const;

    bool isPrintableKey(unsigned int keyval) const;
    char printableChar(unsigned int keyval) const;
    bool hasSelection() const;
    int selectionStart() const;
    int selectionEnd() const;
    void clearSelection();
    void selectRange(int anchor, int focus);
    String selectedText() const;
    void deleteSelection();
    void replaceSelection(const String& text);

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
    virtual void setFocused(bool value) override;
    bool isFocused() const;

    void setFontSize(double size);
    void setTextPadding(double px);

    virtual void draw(ElaraDrawContext* ctx);
    virtual ElaraMouseCursor cursor() const;

    virtual void onMouseMove(double px, double py);
    virtual void onMouseDown(int button, double px, double py);
    virtual void onMouseUp(int button, double px, double py);
    virtual void onKeyDown(unsigned int keyval);
    virtual void onKeyDown(unsigned int keyval, unsigned int modifiers);
    virtual void onKeyUp(unsigned int keyval);
    virtual void onKeyUp(unsigned int keyval, unsigned int modifiers);
    virtual bool performAction(const String& action);
};

}

#endif

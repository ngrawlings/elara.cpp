#ifndef ELARA_RICH_TEXT_EDIT_WIDGET_H
#define ELARA_RICH_TEXT_EDIT_WIDGET_H

#include "../listeners/WidgetListener.h"
#include "ElaraSliderWidget.h"

namespace elara {

class ElaraRichTextEditWidget : public ElaraWidget, public WidgetListener {
private:
    struct ElaraRichTextEditLineMetrics {
        int line_index;
        String visible_text;
        Array<double> caret_positions;

        ElaraRichTextEditLineMetrics()
            : line_index(-1),
              visible_text(""),
              caret_positions() {}
    };

    ElaraSliderWidget* vertical_slider;
    ElaraSliderWidget* horizontal_slider;

    String value;
    String placeholder;
    String palette_master;

    bool enabled;
    bool focused;
    double font_size;
    double line_height;
    double padding_x;
    double padding_y;
    double scrollbar_size;

    int caret_index;
    int preferred_column;
    int scroll_x;
    int scroll_y;
    Array<ElaraRichTextEditLineMetrics> visible_line_metrics;

    ElaraRootWidget* rootWidget() const;
    double charWidth() const;
    double charWidth(double size) const;
    double textViewportWidth() const;
    double textViewportHeight() const;
    int visibleCharCount() const;
    int visibleLineCount() const;
    int lineCount() const;
    int longestLineLength() const;
    void clampCaret();
    void clampScroll();
    void updateScrollbars();
    int lineStartForIndex(int index) const;
    int lineEndForIndex(int index) const;
    int lineIndexForCaret(int index) const;
    int columnForCaret(int index) const;
    int caretIndexForLineColumn(int line_index, int column) const;
    int caretIndexAtPoint(double px, double py) const;
    String lineText(int line_index) const;
    String visibleLineText(const String& line) const;
    double lineFontSize(const String& line) const;
    void rebuildVisibleLineMetrics(ElaraDrawContext* ctx);
    const ElaraRichTextEditLineMetrics* metricsForLine(int line_index) const;
    double visiblePrefixWidth(
        ElaraDrawContext* ctx,
        int line_index,
        const String& line,
        int column
    ) const;
    void insertChar(char c);
    void backspace();
    void deleteForward();
    void moveCaretHorizontal(int delta);
    void moveCaretVertical(int delta);

public:
    ElaraRichTextEditWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraRichTextEditWidget();

    void setText(const String& text_value);
    String getText() const;

    void setPlaceholder(const String& text_value);
    String getPlaceholder() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFocused(bool value);
    bool isFocused() const;

    void setFontSize(double value);
    double getFontSize() const;

    int getScrollX() const;
    int getScrollY() const;

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);
    void onMouseDown(int button, double px, double py);
    void onKeyDown(unsigned int keyval);
    bool eventPropagate(ElaraUiEvent event);

    void onWidgetValueChanged(
        ElaraWidgetHandle handle,
        double value
    );
};

}

#endif

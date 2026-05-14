#ifndef ELARA_CODE_EDITOR_WIDGET_H
#define ELARA_CODE_EDITOR_WIDGET_H

#include "../listeners/WidgetListener.h"
#include "ElaraSliderWidget.h"

namespace elara {

class ElaraCodeEditorWidget : public ElaraWidget, public WidgetListener {
private:

    struct LineDecoration {
        bool breakpoint;
        bool bookmark;
        LineDecoration() : breakpoint(false), bookmark(false) {}
    };

    struct FoldRegion {
        int start_line;
        int end_line;
        bool collapsed;
        FoldRegion() : start_line(0), end_line(0), collapsed(false) {}
        FoldRegion(int s, int e) : start_line(s), end_line(e), collapsed(false) {}
    };

    struct VisibleLineMetrics {
        int logical_line;
        String visible_text;
        Array<double> caret_positions;
        VisibleLineMetrics() : logical_line(-1) {}
    };

    struct Diagnostic {
        int line;
        int column;
        int length;
        String message;
        Diagnostic() : line(0), column(0), length(1) {}
    };

    enum SyntaxStyle {
        SYNTAX_DEFAULT,
        SYNTAX_KEYWORD,
        SYNTAX_TYPE,
        SYNTAX_STRING,
        SYNTAX_COMMENT,
        SYNTAX_NUMBER,
        SYNTAX_PREPROCESSOR,
        SYNTAX_OPERATOR
    };

    struct SyntaxSpan {
        int start_col;
        int end_col;
        SyntaxStyle style;
        SyntaxSpan() : start_col(0), end_col(0), style(SYNTAX_DEFAULT) {}
        SyntaxSpan(int start, int end, SyntaxStyle syntax_style)
            : start_col(start), end_col(end), style(syntax_style) {}
    };

    ElaraSliderWidget* vertical_slider;
    ElaraSliderWidget* horizontal_slider;

    String value;
    String language;
    String palette_master;

    bool enabled;
    bool focused;
    bool selecting;
    bool read_only;
    double font_size;
    double line_height;
    double scrollbar_size;
    double gutter_width;
    double minimap_width;
    double padding_x;

    int caret_index;
    int selection_anchor;
    int selection_focus;
    int preferred_column;
    int scroll_x;
    int scroll_y;

    Array<LineDecoration> decorations;
    Array<FoldRegion> folds;
    Array<int> visible_line_map;
    Array<VisibleLineMetrics> viewport_metrics;
    Array<Diagnostic> diagnostics;

    // Layout
    double effectiveScrollbarW() const;
    double effectiveScrollbarH() const;
    double editorLeft() const;
    double editorRight() const;
    double editorContentWidth() const;
    double minimapLeft() const;

    // Metrics
    double charWidth() const;
    int viewportLineCount() const;
    int viewportCharCount() const;

    // Text helpers
    int logicalLineCount() const;
    int longestVisibleLineLength() const;
    String logicalLineText(int line) const;
    int logicalLineStart(int line) const;
    int logicalLineEnd(int line) const;
    int logicalLineForIndex(int idx) const;
    int columnForIndex(int idx) const;
    int indexForLineColumn(int line, int col) const;

    // Fold
    void rebuildFolds();
    void rebuildVisibleLineMap();
    bool isLineFoldedAway(int logical_line) const;
    FoldRegion* foldStartingAt(int logical_line);
    const FoldRegion* foldStartingAt(int logical_line) const;

    // Decoration
    void ensureDecorations(int logical_line);

    // Scroll
    void clampCaret();
    void clampScroll();
    void updateScrollbars();
    void scrollToCaret();

    // Draw helpers
    void rebuildViewportMetrics(ElaraDrawContext* ctx);
    void drawGutter(ElaraDrawContext* ctx);
    void drawEditor(ElaraDrawContext* ctx);
    void drawMinimap(ElaraDrawContext* ctx);
    ElaraPaletteTriplet syntaxColors(SyntaxStyle style) const;
    Array<SyntaxSpan> tokenizeLine(const String& line) const;

    bool minimap_dragging;

    // Hit testing
    int caretAtPoint(double px, double py) const;
    int gutterLogicalLine(double py) const;

    // Editing
    bool hasSelection() const;
    int selectionStart() const;
    int selectionEnd() const;
    void clearSelection();
    String selectedText() const;
    bool deleteSelection();
    bool replaceSelection(const String& text);
    bool insertText(const String& text);
    bool backspace();
    bool deleteForward();
    void moveCaretHorizontal(int delta);
    void moveCaretVertical(int delta);

    ElaraRootWidget* rootWidget() const;

public:
    ElaraCodeEditorWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );
    virtual ~ElaraCodeEditorWidget();

    void setText(const String& text);
    String getText() const;

    void setEnabled(bool v);
    bool isEnabled() const;

    void setReadOnly(bool v);
    bool isReadOnly() const;

    void setFocused(bool v);
    bool isFocused() const;

    void setFontSize(double size);
    double getFontSize() const;

    void setLanguage(const String& name);
    String getLanguage() const;

    void setBreakpoint(int logical_line, bool v);
    bool hasBreakpoint(int logical_line) const;

    void setBookmark(int logical_line, bool v);
    bool hasBookmark(int logical_line) const;

    void clearDiagnostics();
    void addDiagnostic(int line, int column, int length, const String& message);

    ElaraMouseCursor cursor() const;
    ElaraMouseCursor cursorAt(double px, double py) const;
    void draw(ElaraDrawContext* ctx);
    bool eventPropagate(ElaraUiEvent event);
    void onMouseDown(int button, double px, double py);
    void onMouseMove(double px, double py);
    void onMouseUp(int button, double px, double py);
    void onMouseScroll(double dx, double dy);
    void onKeyDown(unsigned int keyval);
    void onKeyDown(unsigned int keyval, unsigned int modifiers);
    bool performAction(const String& action);

    void onWidgetValueChanged(ElaraWidgetHandle handle, double value);
};

} // namespace elara

#endif

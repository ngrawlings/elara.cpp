#include "ElaraRichTextEditWidget.h"
#include "ElaraRootWidget.h"

namespace elara {

static const unsigned int ELARA_KEY_BACKSPACE = 0xff08;
static const unsigned int ELARA_KEY_RETURN = 0xff0d;
static const unsigned int ELARA_KEY_HOME = 0xff50;
static const unsigned int ELARA_KEY_LEFT = 0xff51;
static const unsigned int ELARA_KEY_UP = 0xff52;
static const unsigned int ELARA_KEY_RIGHT = 0xff53;
static const unsigned int ELARA_KEY_DOWN = 0xff54;
static const unsigned int ELARA_KEY_END = 0xff57;
static const unsigned int ELARA_KEY_DELETE = 0xffff;
static const unsigned int ELARA_KEY_KP_ENTER = 0xff8d;

ElaraRichTextEditWidget::ElaraRichTextEditWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    vertical_slider(new ElaraSliderWidget(
        root_widget,
        ElaraWidgetHandle(
            String((const char*)widget_handle.getHandle().getPtr(), widget_handle.getHandle().length()) +
            String(".vscroll")
        )
    )),
    horizontal_slider(new ElaraSliderWidget(
        root_widget,
        ElaraWidgetHandle(
            String((const char*)widget_handle.getHandle().getPtr(), widget_handle.getHandle().length()) +
            String(".hscroll")
        )
    )),
    value(""),
    placeholder("Rich text"),
    palette_master("input"),
    enabled(true),
    focused(false),
    selecting(false),
    font_size(14),
    line_height(20),
    padding_x(10),
    padding_y(8),
    scrollbar_size(18),
    caret_index(0),
    selection_anchor(0),
    selection_focus(0),
    preferred_column(-1),
    scroll_x(0),
    scroll_y(0) {
    vertical_slider->setOrientation("vertical");
    vertical_slider->setStep(1);
    vertical_slider->setZOrder(10);

    horizontal_slider->setOrientation("horizontal");
    horizontal_slider->setStep(1);
    horizontal_slider->setZOrder(10);

    addChild(Ref<ElaraWidget>(vertical_slider));
    addChild(Ref<ElaraWidget>(horizontal_slider));
}

ElaraRichTextEditWidget::~ElaraRichTextEditWidget() {
}

ElaraRootWidget* ElaraRichTextEditWidget::rootWidget() const {
    ElaraWidget* current = (ElaraWidget*)this;

    while(current) {
        ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(current);

        if(root) {
            return root;
        }

        current = current->getParent();
    }

    return 0;
}

double ElaraRichTextEditWidget::effectiveScrollbarW() const {
    return vertical_slider->isVisible() ? scrollbar_size : 0.0;
}

double ElaraRichTextEditWidget::effectiveScrollbarH() const {
    return horizontal_slider->isVisible() ? scrollbar_size : 0.0;
}

double ElaraRichTextEditWidget::charWidth() const {
    return font_size * 0.58;
}

double ElaraRichTextEditWidget::charWidth(double size) const {
    return size * 0.58;
}

double ElaraRichTextEditWidget::textViewportWidth() const {
    double viewport = width - effectiveScrollbarW() - (padding_x * 2);
    return viewport > 0 ? viewport : 0;
}

double ElaraRichTextEditWidget::textViewportHeight() const {
    double viewport = height - effectiveScrollbarH() - (padding_y * 2);
    return viewport > 0 ? viewport : 0;
}

int ElaraRichTextEditWidget::visibleCharCount() const {
    double chars = textViewportWidth() / charWidth();
    int count = (int)chars;
    return count > 1 ? count : 1;
}

int ElaraRichTextEditWidget::visibleLineCount() const {
    int count = (int)(textViewportHeight() / line_height);
    return count > 1 ? count : 1;
}

int ElaraRichTextEditWidget::lineCount() const {
    int count = 1;

    for(int i = 0; i < value.length(); i++) {
        if(value.byteAt(i) == '\n') {
            count++;
        }
    }

    return count;
}

int ElaraRichTextEditWidget::longestLineLength() const {
    int longest = 0;
    int current = 0;

    for(int i = 0; i < value.length(); i++) {
        if(value.byteAt(i) == '\n') {
            if(current > longest) {
                longest = current;
            }
            current = 0;
        } else {
            current++;
        }
    }

    if(current > longest) {
        longest = current;
    }

    return longest;
}

void ElaraRichTextEditWidget::clampCaret() {
    if(caret_index < 0) {
        caret_index = 0;
    }

    if(caret_index > value.length()) {
        caret_index = (int)value.length();
    }
}

void ElaraRichTextEditWidget::clampScroll() {
    int max_scroll_y = lineCount() - visibleLineCount();
    int max_scroll_x = longestLineLength() - visibleCharCount();

    if(max_scroll_y < 0) {
        max_scroll_y = 0;
    }

    if(max_scroll_x < 0) {
        max_scroll_x = 0;
    }

    if(scroll_y < 0) {
        scroll_y = 0;
    }
    if(scroll_x < 0) {
        scroll_x = 0;
    }
    if(scroll_y > max_scroll_y) {
        scroll_y = max_scroll_y;
    }
    if(scroll_x > max_scroll_x) {
        scroll_x = max_scroll_x;
    }
}

void ElaraRichTextEditWidget::updateScrollbars() {
    clampScroll();

    int max_scroll_y = lineCount() - visibleLineCount();
    int max_scroll_x = longestLineLength() - visibleCharCount();
    if(max_scroll_y < 0) max_scroll_y = 0;
    if(max_scroll_x < 0) max_scroll_x = 0;

    vertical_slider->setVisible(max_scroll_y > 0);
    horizontal_slider->setVisible(max_scroll_x > 0);

    // Second pass: visibility change affects viewport sizes, recompute ranges.
    max_scroll_y = lineCount() - visibleLineCount();
    max_scroll_x = longestLineLength() - visibleCharCount();
    if(max_scroll_y < 0) max_scroll_y = 0;
    if(max_scroll_x < 0) max_scroll_x = 0;

    if(vertical_slider->isVisible()) {
        vertical_slider->setRange(0, max_scroll_y);
        vertical_slider->setStep(1);
        vertical_slider->setValue(scroll_y);
    }
    if(horizontal_slider->isVisible()) {
        horizontal_slider->setRange(0, max_scroll_x);
        horizontal_slider->setStep(1);
        horizontal_slider->setValue(scroll_x);
    }
}

int ElaraRichTextEditWidget::lineStartForIndex(int index) const {
    if(index <= 0) {
        return 0;
    }

    int start = 0;

    for(int i = 0; i < index && i < value.length(); i++) {
        if(value.byteAt(i) == '\n') {
            start = i + 1;
        }
    }

    return start;
}

int ElaraRichTextEditWidget::lineEndForIndex(int index) const {
    int end = lineStartForIndex(index);

    while(end < value.length() && value.byteAt(end) != '\n') {
        end++;
    }

    return end;
}

int ElaraRichTextEditWidget::lineIndexForCaret(int index) const {
    int line = 0;

    for(int i = 0; i < index && i < value.length(); i++) {
        if(value.byteAt(i) == '\n') {
            line++;
        }
    }

    return line;
}

int ElaraRichTextEditWidget::columnForCaret(int index) const {
    return index - lineStartForIndex(index);
}

int ElaraRichTextEditWidget::caretIndexForLineColumn(int line_index, int column) const {
    int current_line = 0;
    int line_start = 0;

    for(int i = 0; i < value.length() && current_line < line_index; i++) {
        if(value.byteAt(i) == '\n') {
            current_line++;
            line_start = i + 1;
        }
    }

    int line_end = line_start;
    while(line_end < value.length() && value.byteAt(line_end) != '\n') {
        line_end++;
    }

    int line_length = line_end - line_start;

    if(column < 0) {
        column = 0;
    }

    if(column > line_length) {
        column = line_length;
    }

    return line_start + column;
}

int ElaraRichTextEditWidget::caretIndexAtPoint(double px, double py) const {
    int line_index = scroll_y + (int)((py - padding_y) / line_height);

    if(line_index < 0) {
        line_index = 0;
    }

    if(line_index >= lineCount()) {
        line_index = lineCount() - 1;
    }

    const ElaraRichTextEditLineMetrics* metrics = metricsForLine(line_index);

    if(metrics) {
        double local_x = px - padding_x;
        int visible_start = scroll_x;

        if(local_x <= 0 || metrics->caret_positions.length() <= 0) {
            return caretIndexForLineColumn(line_index, visible_start);
        }

        for(int i = 0; i < (int)metrics->visible_text.length(); i++) {
            double left = metrics->caret_positions[i];
            double right = metrics->caret_positions[i + 1];
            double midpoint = left + ((right - left) / 2.0);

            if(local_x <= midpoint) {
                return caretIndexForLineColumn(line_index, visible_start + i);
            }
        }

        return caretIndexForLineColumn(
            line_index,
            visible_start + (int)metrics->visible_text.length()
        );
    }

    String line = lineText(line_index);
    double width = charWidth(lineFontSize(line));
    if(width <= 0) {
        width = charWidth();
    }

    int column = scroll_x + (int)((px - padding_x) / width);

    if(column < 0) {
        column = 0;
    }

    return caretIndexForLineColumn(line_index, column);
}

String ElaraRichTextEditWidget::lineText(int line_index) const {
    int start = caretIndexForLineColumn(line_index, 0);
    int end = start;

    while(end < value.length() && value.byteAt(end) != '\n') {
        end++;
    }

    return value.substr(start, end - start);
}

String ElaraRichTextEditWidget::visibleLineText(const String& line) const {
    if(scroll_x >= line.length()) {
        return "";
    }

    String visible = line.substr(scroll_x);
    int max_chars = visibleCharCount();

    if(visible.length() > max_chars) {
        visible = visible.substr(0, max_chars);
    }

    return visible;
}

double ElaraRichTextEditWidget::lineFontSize(const String& line) const {
    String mutable_line(line);

    if(mutable_line.startsWith("## ")) {
        return font_size + 2;
    }

    if(mutable_line.startsWith("# ")) {
        return font_size + 4;
    }

    return font_size;
}

void ElaraRichTextEditWidget::rebuildVisibleLineMetrics(ElaraDrawContext* ctx) {
    visible_line_metrics.clear();

    if(!ctx || value.length() <= 0) {
        return;
    }

    int visible_lines = visibleLineCount();

    for(int i = 0; i < visible_lines; i++) {
        int line_index = scroll_y + i;

        if(line_index >= lineCount()) {
            break;
        }

        String line = lineText(line_index);
        String visible = visibleLineText(line);
        double size = lineFontSize(line);

        ElaraRichTextEditLineMetrics metrics;
        metrics.line_index = line_index;
        metrics.visible_text = visible;
        metrics.caret_positions.push(0.0);

        for(int j = 1; j <= (int)visible.length(); j++) {
            String prefix = visible.substr(0, j);
            metrics.caret_positions.push(ctx->measureTextWidth(prefix, size));
        }

        visible_line_metrics.push(metrics);
    }
}

const ElaraRichTextEditWidget::ElaraRichTextEditLineMetrics*
ElaraRichTextEditWidget::metricsForLine(int line_index) const {
    for(int i = 0; i < (int)visible_line_metrics.length(); i++) {
        if(visible_line_metrics[i].line_index == line_index) {
            return &visible_line_metrics[i];
        }
    }

    return 0;
}

double ElaraRichTextEditWidget::visiblePrefixWidth(
    ElaraDrawContext* ctx,
    int line_index,
    const String& line,
    int column
) const {
    const ElaraRichTextEditLineMetrics* metrics = metricsForLine(line_index);

    if(metrics) {
        int visible_column = column - scroll_x;

        if(visible_column < 0) {
            visible_column = 0;
        }

        if(visible_column > (int)metrics->visible_text.length()) {
            visible_column = (int)metrics->visible_text.length();
        }

        if(visible_column < (int)metrics->caret_positions.length()) {
            return metrics->caret_positions[visible_column];
        }
    }

    if(!ctx) {
        return 0;
    }

    int visible_start = scroll_x;
    int visible_end = column;

    if(visible_start < 0) {
        visible_start = 0;
    }

    if(visible_end < visible_start) {
        visible_end = visible_start;
    }

    if(visible_start > line.length()) {
        visible_start = line.length();
    }

    if(visible_end > line.length()) {
        visible_end = line.length();
    }

    String prefix = line.substr(visible_start, visible_end - visible_start);
    return ctx->measureTextWidth(prefix, lineFontSize(line));
}

bool ElaraRichTextEditWidget::hasSelection() const {
    return selection_anchor != selection_focus;
}

int ElaraRichTextEditWidget::selectionStart() const {
    return selection_anchor < selection_focus ? selection_anchor : selection_focus;
}

int ElaraRichTextEditWidget::selectionEnd() const {
    return selection_anchor > selection_focus ? selection_anchor : selection_focus;
}

void ElaraRichTextEditWidget::clearSelection() {
    selection_anchor = caret_index;
    selection_focus = caret_index;
}

String ElaraRichTextEditWidget::selectedText() const {
    if(!hasSelection()) {
        return String();
    }

    return value.substr(selectionStart(), selectionEnd() - selectionStart());
}

void ElaraRichTextEditWidget::deleteSelection() {
    if(!hasSelection()) {
        return;
    }

    int start = selectionStart();
    int end = selectionEnd();
    value = value.substr(0, start) + value.substr(end);
    caret_index = start;
    clearSelection();
    preferred_column = -1;
}

void ElaraRichTextEditWidget::replaceSelection(const String& text) {
    deleteSelection();

    if(text.length() <= 0) {
        return;
    }

    value = value.substr(0, caret_index) + text + value.substr(caret_index);
    caret_index += (int)text.length();
    clearSelection();
    preferred_column = -1;
}

void ElaraRichTextEditWidget::insertChar(char c) {
    replaceSelection(String(c));
}

void ElaraRichTextEditWidget::backspace() {
    if(hasSelection()) {
        deleteSelection();
        return;
    }

    if(caret_index <= 0) {
        return;
    }

    String before = value.substr(0, caret_index - 1);
    String after = value.substr(caret_index);
    value = before + after;
    caret_index--;
}

void ElaraRichTextEditWidget::deleteForward() {
    if(hasSelection()) {
        deleteSelection();
        return;
    }

    if(caret_index >= value.length()) {
        return;
    }

    String before = value.substr(0, caret_index);
    String after = value.substr(caret_index + 1);
    value = before + after;
}

void ElaraRichTextEditWidget::moveCaretHorizontal(int delta) {
    caret_index += delta;
    clampCaret();
    clearSelection();
    preferred_column = -1;
}

void ElaraRichTextEditWidget::moveCaretVertical(int delta) {
    int current_line = lineIndexForCaret(caret_index);

    if(preferred_column < 0) {
        preferred_column = columnForCaret(caret_index);
    }

    current_line += delta;

    if(current_line < 0) {
        current_line = 0;
    }

    if(current_line >= lineCount()) {
        current_line = lineCount() - 1;
    }

    caret_index = caretIndexForLineColumn(current_line, preferred_column);
    clampCaret();
    clearSelection();
}

void ElaraRichTextEditWidget::setText(const String& text_value) {
    value = text_value;
    caret_index = (int)value.length();
    clearSelection();
    updateScrollbars();
}

String ElaraRichTextEditWidget::getText() const {
    return value;
}

void ElaraRichTextEditWidget::setPlaceholder(const String& text_value) {
    placeholder = text_value;
}

String ElaraRichTextEditWidget::getPlaceholder() const {
    return placeholder;
}

void ElaraRichTextEditWidget::setEnabled(bool value_state) {
    enabled = value_state;
    vertical_slider->setEnabled(value_state);
    horizontal_slider->setEnabled(value_state);

    if(!enabled) {
        selecting = false;
        clearSelection();
    }
}

bool ElaraRichTextEditWidget::isEnabled() const {
    return enabled;
}

void ElaraRichTextEditWidget::setFocused(bool value_state) {
    focused = enabled && value_state;

    if(!focused) {
        selecting = false;
    }
}

bool ElaraRichTextEditWidget::isFocused() const {
    return focused;
}

void ElaraRichTextEditWidget::setFontSize(double value_state) {
    font_size = value_state;
    line_height = font_size + 6;
    updateScrollbars();
}

double ElaraRichTextEditWidget::getFontSize() const {
    return font_size;
}

int ElaraRichTextEditWidget::getScrollX() const {
    return scroll_x;
}

int ElaraRichTextEditWidget::getScrollY() const {
    return scroll_y;
}

ElaraMouseCursor ElaraRichTextEditWidget::cursor() const {
    return enabled ? ELARA_CURSOR_TEXT : ELARA_CURSOR_DEFAULT;
}

void ElaraRichTextEditWidget::draw(ElaraDrawContext* ctx) {
    updateScrollbars();

    if(vertical_slider->isVisible()) {
        vertical_slider->setBounds(width - scrollbar_size, 0, scrollbar_size, height - effectiveScrollbarH());
    }
    if(horizontal_slider->isVisible()) {
        horizontal_slider->setBounds(0, height - scrollbar_size, width - effectiveScrollbarW(), scrollbar_size);
    }

    String sub = enabled ? String("default") : String("disabled");
    ElaraPaletteTriplet c = colors(palette_master, sub);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width - effectiveScrollbarW(), height - effectiveScrollbarH());

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    rebuildVisibleLineMetrics(ctx);

    if(value.length() <= 0) {
        ctx->drawText(padding_x, padding_y + font_size, placeholder, font_size);
    } else {
        int visible_lines = visibleLineCount();

        for(int i = 0; i < visible_lines; i++) {
            int line_index = scroll_y + i;

            if(line_index >= lineCount()) {
                break;
            }

            String line = lineText(line_index);
            String visible = visibleLineText(line);
            double y = padding_y + ((double)i * line_height);
            double size = lineFontSize(line);

            if(hasSelection()) {
                int line_start = caretIndexForLineColumn(line_index, 0);
                int line_end = line_start + (int)line.length();
                int draw_start = selectionStart() > line_start ? selectionStart() : line_start;
                int draw_end = selectionEnd() < line_end ? selectionEnd() : line_end;

                if(draw_end > draw_start) {
                    int start_col = columnForCaret(draw_start);
                    int end_col = columnForCaret(draw_end);
                    double hx = padding_x + visiblePrefixWidth(ctx, line_index, line, start_col);
                    double hw = visiblePrefixWidth(ctx, line_index, line, end_col) -
                        visiblePrefixWidth(ctx, line_index, line, start_col);

                    if(hw > 0) {
                        ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
                        ctx->fillRect(hx, y + 2, hw, line_height - 4);
                        ctx->setColor(c.text.r, c.text.g, c.text.b);
                    }
                }
            }

            ctx->drawText(padding_x, y + size, visible, size);
        }
    }

    if(focused) {
        int caret_line = lineIndexForCaret(caret_index);
        int caret_column = columnForCaret(caret_index);

        if(caret_line >= scroll_y && caret_line < scroll_y + visibleLineCount()) {
            String caret_line_text = lineText(caret_line);
            double caret_size = lineFontSize(caret_line_text);
            double cx = padding_x + visiblePrefixWidth(
                ctx,
                caret_line,
                caret_line_text,
                caret_column
            );
            double cy = padding_y + ((caret_line - scroll_y) * line_height);
            ctx->line(cx, cy + 2, cx, cy + caret_size + 6, 1);
        }
    }

    if(vertical_slider->isVisible()) {
        vertical_slider->onDraw(ctx, (int)scrollbar_size, (int)(height - effectiveScrollbarH()));
    }
    if(horizontal_slider->isVisible()) {
        horizontal_slider->onDraw(ctx, (int)(width - effectiveScrollbarW()), (int)scrollbar_size);
    }
}

bool ElaraRichTextEditWidget::eventPropagate(ElaraUiEvent event) {
    if(vertical_slider->isVisible()) {
        vertical_slider->setBounds(width - scrollbar_size, 0, scrollbar_size, height - effectiveScrollbarH());
    }
    if(horizontal_slider->isVisible()) {
        horizontal_slider->setBounds(0, height - scrollbar_size, width - effectiveScrollbarW(), scrollbar_size);
    }

    bool handled = ElaraWidget::eventPropagate(event);

    if(vertical_slider->isVisible()) scroll_y = (int)vertical_slider->getValue();
    if(horizontal_slider->isVisible()) scroll_x = (int)horizontal_slider->getValue();
    clampScroll();

    return handled;
}

void ElaraRichTextEditWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(button != 1 || !enabled) {
        return;
    }

    if(px <= width - effectiveScrollbarW() && py <= height - effectiveScrollbarH()) {
        caret_index = caretIndexAtPoint(px, py);
        clampCaret();
        selecting = true;
        selection_anchor = caret_index;
        selection_focus = caret_index;
        preferred_column = -1;
        setFocused(true);

        ElaraRootWidget* root = rootWidget();
        if(root) {
            root->setFocus(getHandle());
        }
    }
}

void ElaraRichTextEditWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);

    if(!enabled || !focused || !selecting) {
        return;
    }

    if(px < 0) {
        px = 0;
    }
    if(py < 0) {
        py = 0;
    }
    if(px > width - effectiveScrollbarW()) {
        px = width - effectiveScrollbarW();
    }
    if(py > height - effectiveScrollbarH()) {
        py = height - effectiveScrollbarH();
    }

    caret_index = caretIndexAtPoint(px, py);
    clampCaret();
    selection_focus = caret_index;
    preferred_column = -1;
}

void ElaraRichTextEditWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(button == 1) {
        selecting = false;
    }
}

void ElaraRichTextEditWidget::onKeyDown(unsigned int keyval) {
    onKeyDown(keyval, 0);
}

void ElaraRichTextEditWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    if(!enabled || !focused) {
        return;
    }

    emitKeyDown(keyval);

    if((modifiers & ELARA_KEY_MOD_CTRL) != 0) {
        unsigned int normalized = keyval;
        if(normalized >= 'A' && normalized <= 'Z') {
            normalized = normalized - 'A' + 'a';
        }

        switch(normalized) {
            case 'a':
                selection_anchor = 0;
                selection_focus = (int)value.length();
                caret_index = selection_focus;
                updateScrollbars();
                return;

            case 'c':
                if(hasSelection()) {
                    ElaraRootWidget* root = rootWidget();
                    if(root) {
                        root->setClipboardText(selectedText());
                    }
                }
                return;

            case 'x':
                if(hasSelection()) {
                    ElaraRootWidget* root = rootWidget();
                    if(root) {
                        root->setClipboardText(selectedText());
                    }
                    deleteSelection();
                    updateScrollbars();
                }
                return;

            case 'v': {
                ElaraRootWidget* root = rootWidget();
                String clipboard = root ? root->getClipboardText() : String();
                if(clipboard.length() > 0) {
                    replaceSelection(clipboard);
                    emitKeysTyped(clipboard);
                    updateScrollbars();
                } else if(hasSelection()) {
                    deleteSelection();
                    updateScrollbars();
                }
                return;
            }
        }
    }

    switch(keyval) {
        case ELARA_KEY_BACKSPACE:
            backspace();
            break;

        case ELARA_KEY_DELETE:
            deleteForward();
            break;

        case ELARA_KEY_LEFT:
            moveCaretHorizontal(-1);
            break;

        case ELARA_KEY_RIGHT:
            moveCaretHorizontal(1);
            break;

        case ELARA_KEY_UP:
            moveCaretVertical(-1);
            break;

        case ELARA_KEY_DOWN:
            moveCaretVertical(1);
            break;

        case ELARA_KEY_HOME:
            caret_index = lineStartForIndex(caret_index);
            clearSelection();
            preferred_column = -1;
            break;

        case ELARA_KEY_END:
            caret_index = lineEndForIndex(caret_index);
            clearSelection();
            preferred_column = -1;
            break;

        case ELARA_KEY_RETURN:
        case ELARA_KEY_KP_ENTER:
            insertChar('\n');
            preferred_column = -1;
            break;

        default:
            if(keyval >= 32 && keyval <= 126) {
                insertChar((char)keyval);
                emitKeysTyped(String((char)keyval));
                preferred_column = -1;
            }
            break;
    }

    clampCaret();

    int caret_line = lineIndexForCaret(caret_index);
    int caret_column = columnForCaret(caret_index);

    if(caret_line < scroll_y) {
        scroll_y = caret_line;
    } else if(caret_line >= scroll_y + visibleLineCount()) {
        scroll_y = caret_line - visibleLineCount() + 1;
    }

    if(caret_column < scroll_x) {
        scroll_x = caret_column;
    } else if(caret_column >= scroll_x + visibleCharCount()) {
        scroll_x = caret_column - visibleCharCount() + 1;
    }

    updateScrollbars();
}

void ElaraRichTextEditWidget::onWidgetValueChanged(
    ElaraWidgetHandle handle,
    double value_state
) {
    Memory memory = handle.getHandle();
    String handle_text((const char*)memory.getPtr(), memory.length());

    if(handle_text == String(getHandle().getHandle().getPtr(), getHandle().getHandle().length()) + String(".vscroll")) {
        scroll_y = (int)value_state;
        clampScroll();
        return;
    }

    if(handle_text == String(getHandle().getHandle().getPtr(), getHandle().getHandle().length()) + String(".hscroll")) {
        scroll_x = (int)value_state;
        clampScroll();
    }
}

bool ElaraRichTextEditWidget::performAction(const String& action) {
    if(!enabled || !focused) {
        return false;
    }

    if(action == String("edit.select_all")) {
        selection_anchor = 0;
        selection_focus = (int)value.length();
        caret_index = selection_focus;
        updateScrollbars();
        return true;
    }

    if(action == String("edit.copy")) {
        if(hasSelection()) {
            ElaraRootWidget* root = rootWidget();
            if(root) {
                root->setClipboardText(selectedText());
            }
        }
        return true;
    }

    if(action == String("edit.cut")) {
        if(hasSelection()) {
            ElaraRootWidget* root = rootWidget();
            if(root) {
                root->setClipboardText(selectedText());
            }
            deleteSelection();
            updateScrollbars();
        }
        return true;
    }

    if(action == String("edit.paste")) {
        ElaraRootWidget* root = rootWidget();
        String clipboard = root ? root->getClipboardText() : String();
        if(clipboard.length() > 0) {
            replaceSelection(clipboard);
            emitKeysTyped(clipboard);
            updateScrollbars();
        } else if(hasSelection()) {
            deleteSelection();
            updateScrollbars();
        }
        return true;
    }

    return false;
}

}

#include "ElaraTextInputWidget.h"
#include "ElaraRootWidget.h"

namespace elara {

static const unsigned int ELARA_KEY_BACKSPACE = 0xff08;
static const unsigned int ELARA_KEY_TAB = 0xff09;
static const unsigned int ELARA_KEY_RETURN = 0xff0d;
static const unsigned int ELARA_KEY_ESCAPE = 0xff1b;
static const unsigned int ELARA_KEY_HOME = 0xff50;
static const unsigned int ELARA_KEY_LEFT = 0xff51;
static const unsigned int ELARA_KEY_RIGHT = 0xff53;
static const unsigned int ELARA_KEY_END = 0xff57;
static const unsigned int ELARA_KEY_DELETE = 0xffff;
static const unsigned int ELARA_KEY_KP_ENTER = 0xff8d;

namespace {

ElaraRootWidget* rootWidgetFor(ElaraWidget* widget) {
    ElaraWidget* current = widget;

    while(current) {
        ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(current);

        if(root) {
            return root;
        }

        current = current->getParent();
    }

    return 0;
}

}

ElaraTextInputWidget::ElaraTextInputWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    value(""),
    placeholder("Text input"),
    palette_master("input"),
    enabled(true),
    focused(false),
    key_down(false),
    last_keyval(0),
    selecting(false),
    font_size(14),
    padding_x(10),
    caret_padding(2),
    caret_index(0),
    selection_anchor(0),
    selection_focus(0) {}

ElaraTextInputWidget::~ElaraTextInputWidget() {}

void ElaraTextInputWidget::setText(const String& text_value) {
    value = text_value;
    caret_index = (int)value.length();
    selection_anchor = caret_index;
    selection_focus = caret_index;
    clampCaret();
}

String ElaraTextInputWidget::getText() const {
    return value;
}

void ElaraTextInputWidget::setPlaceholder(const String& text_value) {
    placeholder = text_value;
}

String ElaraTextInputWidget::getPlaceholder() const {
    return placeholder;
}

ElaraMouseCursor ElaraTextInputWidget::cursor() const {
    return enabled ? ELARA_CURSOR_TEXT : ELARA_CURSOR_DEFAULT;
}

void ElaraTextInputWidget::setEnabled(bool input_enabled) {
    enabled = input_enabled;

    if(!enabled) {
        focused = false;
        key_down = false;
        last_keyval = 0;
        selecting = false;
        clearSelection();
    }
}

bool ElaraTextInputWidget::isEnabled() const {
    return enabled;
}

void ElaraTextInputWidget::setFocused(bool value) {
    focused = enabled && value;

    if(!focused) {
        key_down = false;
        last_keyval = 0;
        selecting = false;
    }
}

bool ElaraTextInputWidget::isFocused() const {
    return focused;
}

void ElaraTextInputWidget::setFontSize(double size) {
    font_size = size;
}

void ElaraTextInputWidget::setTextPadding(double px) {
    padding_x = px;
}

double ElaraTextInputWidget::textViewportWidth() const {
    double viewport = width - (padding_x * 2) - caret_padding;
    return viewport > 0 ? viewport : 0;
}

double ElaraTextInputWidget::measuredTextWidth(ElaraDrawContext* ctx, const String& text) const {
    if(ctx) {
        return ctx->measureTextWidth(text, font_size);
    }

    return estimateTextWidth(text);
}

double ElaraTextInputWidget::estimateTextWidth(const String& text) const {
    return text.length() * font_size * 0.58;
}

double ElaraTextInputWidget::textY() const {
    return (height / 2) + (font_size / 2) - 2;
}

void ElaraTextInputWidget::rebuildMetrics(ElaraDrawContext* ctx) {
    double viewport = textViewportWidth();
    int start = caret_index;
    int end = 0;

    metrics_cache.visible_start = 0;
    metrics_cache.visible_text = "";
    metrics_cache.caret_positions.clear();
    metrics_cache.caret_positions.push(0.0);

    if(viewport <= 0 || value.length() <= 0) {
        return;
    }

    if(start < 0) {
        start = 0;
    }
    if(start > (int)value.length()) {
        start = (int)value.length();
    }

    while(start > 0) {
        String candidate = value.substr(start - 1, caret_index - start + 1);

        if(measuredTextWidth(ctx, candidate) > viewport) {
            break;
        }

        start--;
    }

    end = start;
    while(end < (int)value.length()) {
        String candidate = value.substr(start, end - start + 1);

        if(measuredTextWidth(ctx, candidate) > viewport) {
            break;
        }

        end++;
    }

    metrics_cache.visible_start = start;
    metrics_cache.visible_text = value.substr(start, end - start);

    for(int i = 1; i <= (int)metrics_cache.visible_text.length(); i++) {
        metrics_cache.caret_positions.push(
            measuredTextWidth(ctx, metrics_cache.visible_text.substr(0, i))
        );
    }
}

int ElaraTextInputWidget::caretIndexAtX(double px) const {
    int start = metrics_cache.visible_start;

    if(px <= padding_x) {
        return start;
    }

    if(metrics_cache.caret_positions.length() <= 0) {
        return start;
    }

    for(int i = 0; i < (int)metrics_cache.caret_positions.length(); i++) {
        double caret_x = padding_x + metrics_cache.caret_positions[i] + caret_padding;

        if(px <= caret_x) {
            return start + i;
        }
    }

    return start + (int)metrics_cache.visible_text.length();
}

bool ElaraTextInputWidget::isPrintableKey(unsigned int keyval) const {
    return keyval >= 32 && keyval <= 126;
}

char ElaraTextInputWidget::printableChar(unsigned int keyval) const {
    if(!isPrintableKey(keyval)) {
        return 0;
    }

    return (char)keyval;
}

bool ElaraTextInputWidget::hasSelection() const {
    return selection_anchor != selection_focus;
}

int ElaraTextInputWidget::selectionStart() const {
    return selection_anchor < selection_focus ? selection_anchor : selection_focus;
}

int ElaraTextInputWidget::selectionEnd() const {
    return selection_anchor > selection_focus ? selection_anchor : selection_focus;
}

void ElaraTextInputWidget::clearSelection() {
    selection_anchor = caret_index;
    selection_focus = caret_index;
}

void ElaraTextInputWidget::selectRange(int anchor, int focus) {
    selection_anchor = anchor;
    selection_focus = focus;
}

String ElaraTextInputWidget::selectedText() const {
    if(!hasSelection()) {
        return String();
    }

    int start = selectionStart();
    int end = selectionEnd();
    return value.substr(start, end - start);
}

void ElaraTextInputWidget::deleteSelection() {
    if(!hasSelection()) {
        return;
    }

    int start = selectionStart();
    int end = selectionEnd();
    String before = value.substr(0, start);
    String after = value.substr(end);
    value = before + after;
    caret_index = start;
    clearSelection();
}

void ElaraTextInputWidget::replaceSelection(const String& text) {
    deleteSelection();

    if(text.length() <= 0) {
        return;
    }

    String before = value.substr(0, caret_index);
    String after = value.substr(caret_index);
    value = before + text + after;
    caret_index += (int)text.length();
    clearSelection();
}

void ElaraTextInputWidget::clampCaret() {
    if(caret_index < 0) {
        caret_index = 0;
    }

    if(caret_index > (int)value.length()) {
        caret_index = (int)value.length();
    }
}

void ElaraTextInputWidget::insertChar(char c) {
    if(!c) {
        return;
    }

    clampCaret();
    String ch(c);
    replaceSelection(ch);
}

void ElaraTextInputWidget::backspace() {
    clampCaret();

    if(hasSelection()) {
        deleteSelection();
        return;
    }

    if(caret_index <= 0 || value.length() <= 0) {
        return;
    }

    String before = value.substr(0, caret_index - 1);
    String after = value.substr(caret_index);

    value = before + after;
    caret_index--;
}

void ElaraTextInputWidget::deleteForward() {
    clampCaret();

    if(hasSelection()) {
        deleteSelection();
        return;
    }

    if(caret_index < 0 || caret_index >= (int)value.length()) {
        return;
    }

    String before = value.substr(0, caret_index);
    String after = value.substr(caret_index + 1);

    value = before + after;
}

void ElaraTextInputWidget::draw(ElaraDrawContext* ctx) {
    String sub("default");

    if(!enabled) {
        sub = String("disabled");
    }

    ElaraPaletteTriplet c = colors(palette_master, sub);

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    rebuildMetrics(ctx);

    if(value.length() > 0) {
        if(hasSelection()) {
            int visible_start = metrics_cache.visible_start;
            int visible_end = visible_start + (int)metrics_cache.visible_text.length();
            int select_start = selectionStart();
            int select_end = selectionEnd();
            int draw_start = select_start > visible_start ? select_start : visible_start;
            int draw_end = select_end < visible_end ? select_end : visible_end;

            if(draw_end > draw_start) {
                int start_column = draw_start - visible_start;
                int end_column = draw_end - visible_start;
                double highlight_x = padding_x + caret_padding + metrics_cache.caret_positions[start_column];
                double highlight_w = metrics_cache.caret_positions[end_column] - metrics_cache.caret_positions[start_column];

                ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
                ctx->fillRect(highlight_x, 6, highlight_w, height - 12);
                ctx->setColor(c.text.r, c.text.g, c.text.b);
            }
        }

        ctx->drawText(padding_x, textY(), metrics_cache.visible_text, font_size);
    } else {
        String visible_placeholder = placeholder;
        while(visible_placeholder.length() > 0 && measuredTextWidth(ctx, visible_placeholder) > textViewportWidth()) {
            visible_placeholder = visible_placeholder.substr(0, visible_placeholder.length() - 1);
        }
        ctx->drawText(padding_x, textY(), visible_placeholder, font_size);
    }

    if(enabled && focused) {
        int visible_column = caret_index - metrics_cache.visible_start;
        double caret_x = padding_x + caret_padding;

        if(visible_column < 0) {
            visible_column = 0;
        }

        if(visible_column >= (int)metrics_cache.caret_positions.length()) {
            visible_column = (int)metrics_cache.caret_positions.length() - 1;
        }

        if(visible_column >= 0 && visible_column < (int)metrics_cache.caret_positions.length()) {
            caret_x += metrics_cache.caret_positions[visible_column];
        }

        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->line(caret_x, 7, caret_x, height - 7, 1);
    }
}

void ElaraTextInputWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(button != 1) {
        return;
    }

    if(!enabled) {
        setFocused(false);
        return;
    }

    if(containsLocal(px, py)) {
        caret_index = caretIndexAtX(px);
        clampCaret();
        selecting = true;
        selectRange(caret_index, caret_index);
        setFocused(true);

        ElaraRootWidget* root = rootWidgetFor(this);
        if(root) {
            root->setFocus(getHandle());
        }
        return;
    }

    selecting = false;
    clearSelection();
    setFocused(false);
}

void ElaraTextInputWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);

    if(!enabled || !focused || !selecting) {
        return;
    }

    caret_index = caretIndexAtX(px);
    clampCaret();
    selection_focus = caret_index;
}

void ElaraTextInputWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
    if(button == 1) {
        selecting = false;
    }
}

void ElaraTextInputWidget::onKeyDown(unsigned int keyval) {
    onKeyDown(keyval, 0);
}

void ElaraTextInputWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    if(!enabled || !focused) {
        return;
    }

    emitKeyDown(keyval);

    key_down = true;
    last_keyval = keyval;

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
                return;

            case 'c':
                if(hasSelection()) {
                    ElaraRootWidget* root = rootWidgetFor(this);
                    if(root) {
                        root->setClipboardText(selectedText());
                    }
                }
                return;

            case 'x':
                if(hasSelection()) {
                    ElaraRootWidget* root = rootWidgetFor(this);
                    if(root) {
                        root->setClipboardText(selectedText());
                    }
                    deleteSelection();
                }
                return;

            case 'v': {
                ElaraRootWidget* root = rootWidgetFor(this);
                String clipboard = root ? root->getClipboardText() : String();
                if(clipboard.length() > 0) {
                    replaceSelection(clipboard);
                    emitKeysTyped(clipboard);
                } else if(hasSelection()) {
                    deleteSelection();
                }
                return;
            }
        }
    }

    switch(keyval) {
        case ELARA_KEY_BACKSPACE:
            backspace();
            return;

        case ELARA_KEY_DELETE:
            deleteForward();
            return;

        case ELARA_KEY_LEFT:
            caret_index--;
            clampCaret();
            clearSelection();
            return;

        case ELARA_KEY_RIGHT:
            caret_index++;
            clampCaret();
            clearSelection();
            return;

        case ELARA_KEY_HOME:
            caret_index = 0;
            clearSelection();
            return;

        case ELARA_KEY_END:
            caret_index = (int)value.length();
            clearSelection();
            return;

        case ELARA_KEY_TAB:
        case ELARA_KEY_RETURN:
        case ELARA_KEY_KP_ENTER:
        case ELARA_KEY_ESCAPE:
            return;
    }

    char ch = printableChar(keyval);
    insertChar(ch);

    if(ch) {
        emitKeysTyped(String(ch));
    }
}

void ElaraTextInputWidget::onKeyUp(unsigned int keyval) {
    onKeyUp(keyval, 0);
}

void ElaraTextInputWidget::onKeyUp(unsigned int keyval, unsigned int modifiers) {
    (void)modifiers;

    if(!enabled || !focused) {
        return;
    }

    emitKeyUp(keyval);

    if(last_keyval == keyval) {
        key_down = false;
        last_keyval = 0;
    }
}

bool ElaraTextInputWidget::performAction(const String& action) {
    if(!enabled || !focused) {
        return false;
    }

    if(action == String("edit.select_all")) {
        selection_anchor = 0;
        selection_focus = (int)value.length();
        caret_index = selection_focus;
        return true;
    }

    if(action == String("edit.copy")) {
        if(hasSelection()) {
            ElaraRootWidget* root = rootWidgetFor(this);
            if(root) {
                root->setClipboardText(selectedText());
            }
        }
        return true;
    }

    if(action == String("edit.cut")) {
        if(hasSelection()) {
            ElaraRootWidget* root = rootWidgetFor(this);
            if(root) {
                root->setClipboardText(selectedText());
            }
            deleteSelection();
        }
        return true;
    }

    if(action == String("edit.paste")) {
        ElaraRootWidget* root = rootWidgetFor(this);
        String clipboard = root ? root->getClipboardText() : String();
        if(clipboard.length() > 0) {
            replaceSelection(clipboard);
            emitKeysTyped(clipboard);
        } else if(hasSelection()) {
            deleteSelection();
        }
        return true;
    }

    return false;
}

}

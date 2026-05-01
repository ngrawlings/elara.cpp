#include "ElaraTextInputWidget.h"

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

ElaraTextInputWidget::ElaraTextInputWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    value(""),
    placeholder("Text input"),
    palette_master("input"),
    enabled(true),
    key_down(false),
    last_keyval(0),
    font_size(14),
    padding_x(10),
    caret_padding(2),
    caret_index(0) {}

ElaraTextInputWidget::~ElaraTextInputWidget() {}

void ElaraTextInputWidget::setText(const String& text_value) {
    value = text_value;
    caret_index = (int)value.length();
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

void ElaraTextInputWidget::setEnabled(bool input_enabled) {
    enabled = input_enabled;

    if(!enabled) {
        key_down = false;
        last_keyval = 0;
    }
}

bool ElaraTextInputWidget::isEnabled() const {
    return enabled;
}

void ElaraTextInputWidget::setFontSize(double size) {
    font_size = size;
}

void ElaraTextInputWidget::setTextPadding(double px) {
    padding_x = px;
}

double ElaraTextInputWidget::estimateTextWidth(const String& text) const {
    return text.length() * font_size * 0.58;
}

double ElaraTextInputWidget::textY() const {
    return (height / 2) + (font_size / 2) - 2;
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

    String before = value.substr(0, caret_index);
    String after = value.substr(caret_index);
    String ch(c);

    value = before + ch + after;
    caret_index++;
}

void ElaraTextInputWidget::backspace() {
    clampCaret();

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

    if(value.length() > 0) {
        ctx->drawText(padding_x, textY(), value, font_size);
    } else {
        ctx->drawText(padding_x, textY(), placeholder, font_size);
    }

    if(enabled) {
        String before = value.substr(0, caret_index);
        double caret_x = padding_x + estimateTextWidth(before) + caret_padding;

        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->line(caret_x, 7, caret_x, height - 7, 1);
    }
}

void ElaraTextInputWidget::onKeyDown(unsigned int keyval) {
    if(!enabled) {
        return;
    }

    key_down = true;
    last_keyval = keyval;

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
            return;

        case ELARA_KEY_RIGHT:
            caret_index++;
            clampCaret();
            return;

        case ELARA_KEY_HOME:
            caret_index = 0;
            return;

        case ELARA_KEY_END:
            caret_index = (int)value.length();
            return;

        case ELARA_KEY_TAB:
        case ELARA_KEY_RETURN:
        case ELARA_KEY_KP_ENTER:
        case ELARA_KEY_ESCAPE:
            return;
    }

    insertChar(printableChar(keyval));
}

void ElaraTextInputWidget::onKeyUp(unsigned int keyval) {
    if(last_keyval == keyval) {
        key_down = false;
        last_keyval = 0;
    }
}

}

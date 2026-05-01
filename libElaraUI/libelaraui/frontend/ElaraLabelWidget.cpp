#include "ElaraLabelWidget.h"

namespace elara {

ElaraLabelWidget::ElaraLabelWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    text(""),
    palette_master("label"),
    palette_sub("default"),
    font_size(14),
    padding_x(8),
    padding_y(6),
    horizontal_align(ELARA_LABEL_ALIGN_LEFT),
    vertical_align(ELARA_LABEL_ALIGN_MIDDLE),
    draw_background(false) {}

ElaraLabelWidget::~ElaraLabelWidget() {}

void ElaraLabelWidget::setText(const String& label_text) {
    text = label_text;
}

String ElaraLabelWidget::getText() const {
    return text;
}

void ElaraLabelWidget::setFontSize(double size) {
    font_size = size;
}

double ElaraLabelWidget::getFontSize() const {
    return font_size;
}

void ElaraLabelWidget::setPadding(double px, double py) {
    padding_x = px;
    padding_y = py;
}

void ElaraLabelWidget::setHorizontalAlign(ElaraLabelHorizontalAlign align) {
    horizontal_align = align;
}

void ElaraLabelWidget::setVerticalAlign(ElaraLabelVerticalAlign align) {
    vertical_align = align;
}

void ElaraLabelWidget::setPaletteProfile(const String& master, const String& sub) {
    palette_master = master;
    palette_sub = sub;
}

void ElaraLabelWidget::setDrawBackground(bool enabled) {
    draw_background = enabled;
}

bool ElaraLabelWidget::getDrawBackground() const {
    return draw_background;
}

double ElaraLabelWidget::estimateTextWidth() const {
    return text.length() * font_size * 0.58;
}

double ElaraLabelWidget::textX() const {
    double text_width = estimateTextWidth();

    if(horizontal_align == ELARA_LABEL_ALIGN_CENTER) {
        return (width - text_width) / 2;
    }

    if(horizontal_align == ELARA_LABEL_ALIGN_RIGHT) {
        return width - text_width - padding_x;
    }

    return padding_x;
}

double ElaraLabelWidget::textY() const {
    if(vertical_align == ELARA_LABEL_ALIGN_TOP) {
        return padding_y + font_size;
    }

    if(vertical_align == ELARA_LABEL_ALIGN_BOTTOM) {
        return height - padding_y;
    }

    return (height / 2) + (font_size / 2) - 2;
}

void ElaraLabelWidget::draw(ElaraDrawContext* ctx) {
    ElaraPaletteTriplet c = colors(palette_master, palette_sub);

    if(draw_background) {
        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);
    }

    ctx->setColor(c.text.r, c.text.g, c.text.b);
    ctx->drawText(textX(), textY(), text, font_size);
}

}

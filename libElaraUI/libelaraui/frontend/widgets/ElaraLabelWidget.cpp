#include "ElaraLabelWidget.h"

namespace elara {

ElaraLabelWidget::ElaraLabelWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    text(""),
    palette_master("label"),
    palette_sub("default"),
    has_text_color_override(false),
    text_color_override(),
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

void ElaraLabelWidget::setTextColorOverride(const ElaraColor& color) {
    text_color_override = color;
    has_text_color_override = true;
}

void ElaraLabelWidget::setForegroundColorOverride(const ElaraColor& color) {
    setTextColorOverride(color);
}

void ElaraLabelWidget::clearTextColorOverride() {
    has_text_color_override = false;
}

void ElaraLabelWidget::clearForegroundColorOverride() {
    clearTextColorOverride();
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

    if(has_text_color_override) {
        ctx->setColor(
            text_color_override.r,
            text_color_override.g,
            text_color_override.b
        );
    } else {
        ctx->setColor(c.text.r, c.text.g, c.text.b);
    }

    if(text.indexOf("\n") < 0) {
        ctx->drawText(textX(), textY(), text, font_size);
        return;
    }

    double line_height = font_size * 1.4;

    int num_lines = 1;
    int scan = 0;
    while((scan = text.indexOf("\n", scan)) >= 0) {
        num_lines++;
        scan++;
    }

    double y_start;
    if(vertical_align == ELARA_LABEL_ALIGN_TOP) {
        y_start = padding_y + font_size;
    } else if(vertical_align == ELARA_LABEL_ALIGN_BOTTOM) {
        y_start = height - padding_y - (num_lines - 1) * line_height;
    } else {
        double total_height = (double)num_lines * line_height;
        y_start = (height - total_height) / 2.0 + font_size;
    }

    int start = 0;
    double y = y_start;
    while(true) {
        int nl = text.indexOf("\n", start);
        String line = (nl < 0) ? text.substr(start) : text.substr(start, nl - start);

        double lw = (double)line.length() * font_size * 0.58;
        double lx;
        if(horizontal_align == ELARA_LABEL_ALIGN_CENTER) {
            lx = (width - lw) / 2.0;
        } else if(horizontal_align == ELARA_LABEL_ALIGN_RIGHT) {
            lx = width - lw - padding_x;
        } else {
            lx = padding_x;
        }

        ctx->drawText(lx, y, line, font_size);

        if(nl < 0) break;
        start = nl + 1;
        y += line_height;
    }
}

}

#include "ElaraWidget.h"

namespace elara {

ElaraWidget::ElaraWidget()
    : x(0),
      y(0),
      width(0),
      height(0),
      palette(0) {}

ElaraWidget::~ElaraWidget() {}

void ElaraWidget::setPalette(ElaraPalette* widget_palette) {
    palette = widget_palette;
}

ElaraPalette* ElaraWidget::getPalette() const {
    return palette;
}

ElaraPaletteTriplet ElaraWidget::colors(
    const String& master,
    const String& sub
) const {
    if(palette) {
        return palette->get(master, sub);
    }

    return ElaraPaletteTriplet();
}

ElaraColor ElaraWidget::base(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).base;
}

ElaraColor ElaraWidget::accent(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).accent;
}

ElaraColor ElaraWidget::text(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).text;
}

void ElaraWidget::setBounds(double px, double py, double w, double h) {
    x = px;
    y = py;
    width = w;
    height = h;
}

void ElaraWidget::onDraw(
    ElaraDrawContext* ctx,
    int draw_width,
    int draw_height
) {
    x = 0;
    y = 0;
    width = draw_width;
    height = draw_height;

    draw(ctx);
}

bool ElaraWidget::contains(double px, double py) const {
    return px >= x && py >= y && px <= x + width && py <= y + height;
}

}
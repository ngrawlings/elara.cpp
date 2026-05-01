#include "ElaraWidget.h"

namespace elara {

    ElaraWidget::ElaraWidget() : x(0), y(0), width(0), height(0) {}
    ElaraWidget::~ElaraWidget() {}

    void ElaraWidget::setBounds(double px, double py, double w, double h) {
            x = px;
            y = py;
            width = w;
            height = h;
        }

    void ElaraWidget::onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height) {
            draw(ctx);
    }

    bool ElaraWidget::contains(double px, double py) const {
        return px >= x && py >= y && px <= x + width && py <= y + height;
    }

}
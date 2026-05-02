#include "ElaraCanvasWidget.h"

namespace elara {

ElaraCanvasWidget::ElaraCanvasWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    palette_master("panel") {}

ElaraCanvasWidget::~ElaraCanvasWidget() {}

void ElaraCanvasWidget::setPaletteMaster(const String& master) {
    palette_master = master;
}

String ElaraCanvasWidget::getPaletteMaster() const {
    return palette_master;
}

void ElaraCanvasWidget::draw(ElaraDrawContext* ctx) {
    ElaraPaletteTriplet c = colors(palette_master, "default");

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    drawCanvas(ctx);
}

}

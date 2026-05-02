#ifndef ELARA_CANVAS_WIDGET_H
#define ELARA_CANVAS_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraCanvasWidget : public ElaraWidget {
private:
    String palette_master;

public:
    ElaraCanvasWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraCanvasWidget();

    void setPaletteMaster(const String& master);
    String getPaletteMaster() const;

    void draw(ElaraDrawContext* ctx);

protected:
    virtual void drawCanvas(ElaraDrawContext* ctx) = 0;
};

}

#endif

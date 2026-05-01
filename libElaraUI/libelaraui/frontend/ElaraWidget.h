#ifndef ELARA_WIDGET_H
#define ELARA_WIDGET_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "../ElaraGui.h"

namespace elara {

class ElaraWidget : public ElaraDrawSurface {
protected:
    double x;
    double y;
    double width;
    double height;

public:
    ElaraWidget();
    virtual ~ElaraWidget();

    // Draw Surface
    virtual void setBounds(double px, double py, double w, double h);
    virtual void onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height);
    virtual void draw(ElaraDrawContext* ctx) = 0;

    virtual void onMouseMove(double px, double py) {}
    virtual void onMouseDown(int button, double px, double py) {}
    virtual void onMouseUp(int button, double px, double py) {}
    virtual void onKeyDown(unsigned int keyval) {}
    virtual void onKeyUp(unsigned int keyval) {}

    bool contains(double px, double py) const;

};

}

#endif
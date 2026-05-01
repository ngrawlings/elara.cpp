#ifndef ELARA_WIDGET_H
#define ELARA_WIDGET_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "../ElaraGui.h"
#include "ElaraWidgetHandle.h"
#include "ElaraPalette.h"

namespace elara {

enum ElaraUiEventType {
    ELARA_UI_MOUSE_MOVE,
    ELARA_UI_MOUSE_DOWN,
    ELARA_UI_MOUSE_UP,
    ELARA_UI_KEY_DOWN,
    ELARA_UI_KEY_UP
};

class ElaraRootWidget;

class ElaraWidgetRegister {
public:
    virtual void registerWidget(ElaraWidgetHandle widget_handle, void* widget) = 0;
};

class ElaraUiEvent {
public:
    ElaraRootWidget* root_widget;

    ElaraUiEventType type;

    double x;
    double y;

    int button;
    unsigned int keyval;

    ElaraUiEvent()
        : type(ELARA_UI_MOUSE_MOVE),
          x(0),
          y(0),
          button(0),
          keyval(0) {}
};

class ElaraWidget : public ElaraDrawSurface {
protected:
    ElaraWidgetHandle widget_handle;

    double x;
    double y;
    double width;
    double height;

    int z_order;

    ElaraPalette* palette;
    Array< Ref<ElaraWidget> > children;

    ElaraWidget();

public:
    ElaraWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);
    virtual ~ElaraWidget();

    virtual void addChild(Ref<ElaraWidget> child);
    virtual int childCount() const;
    virtual Ref<ElaraWidget> getChild(int index) const;

    virtual void setZOrder(int z);
    virtual int getZOrder() const;

    virtual void setPalette(ElaraPalette* widget_palette);
    virtual ElaraPalette* getPalette() const;

virtual ElaraPaletteTriplet colors(
        const String& master,
        const String& sub
    ) const;

    virtual ElaraColor base(
        const String& master,
        const String& sub
    ) const;

    virtual ElaraColor accent(
        const String& master,
        const String& sub
    ) const;

    virtual ElaraColor text(
        const String& master,
        const String& sub
    ) const;

    virtual void setBounds(double px, double py, double w, double h);
    bool contains(double px, double py) const;
    bool containsLocal(double px, double py) const;

    virtual bool eventPropagate(ElaraUiEvent event);
    virtual bool handleEvent(const ElaraUiEvent& event);

    virtual void onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height);
    virtual void draw(ElaraDrawContext* ctx) = 0;

    virtual void onMouseMove(double px, double py) {}
    virtual void onMouseDown(int button, double px, double py) {}
    virtual void onMouseUp(int button, double px, double py) {}
    virtual void onKeyDown(unsigned int keyval) {}
    virtual void onKeyUp(unsigned int keyval) {}
};

}

#endif
#ifndef ELARA_WIDGET_H
#define ELARA_WIDGET_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include "../../ElaraGui.h"
#include "../ElaraWidgetHandle.h"
#include "../theme/ElaraPalette.h"
#include "../listeners/WidgetListener.h"

namespace elara {

enum ElaraUiEventType {
    ELARA_UI_MOUSE_MOVE,
    ELARA_UI_MOUSE_DOWN,
    ELARA_UI_MOUSE_UP,
    ELARA_UI_MOUSE_DOUBLE_CLICK,
    ELARA_UI_MOUSE_SCROLL,
    ELARA_UI_KEY_DOWN,
    ELARA_UI_KEY_UP,
    ELARA_UI_KEYS_TYPED
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
    String text;
    double scroll_dx;
    double scroll_dy;

    ElaraUiEvent()
        : root_widget(0),
          type(ELARA_UI_MOUSE_MOVE),
          x(0),
          y(0),
          button(0),
          keyval(0),
          scroll_dx(0),
          scroll_dy(0) {}
};

class ElaraWidget : public ElaraDrawSurface {
protected:
    ElaraWidgetHandle widget_handle;

    bool visible;
    bool hovered;
    bool mouse_down;
    int hovered_child_index;

    double x;
    double y;
    double width;
    double height;

    double margin_left;
    double margin_top;
    double margin_right;
    double margin_bottom;

    double padding_left;
    double padding_top;
    double padding_right;
    double padding_bottom;

    int z_order;

    ElaraWidget* parent;
    ElaraPalette* palette;
    Array< Ref<ElaraWidget> > children;
    Array< Ref<WidgetListener> > listeners;

    ElaraWidget();

    virtual void emitMouseMove(double px, double py);
    virtual void emitMouseDown(int button, double px, double py);
    virtual void emitMouseUp(int button, double px, double py);
    virtual void emitClicked(int button, double px, double py);
    virtual void emitHoverChanged(bool is_hovered);
    virtual void emitKeyDown(unsigned int keyval);
    virtual void emitKeyUp(unsigned int keyval);
    virtual void emitKeysTyped(const String& text);
    virtual void emitTextChanged(const String& text);
    virtual void emitValueChanged(double value);
    virtual void emitAction(const String& action);

public:
    ElaraWidget(ElaraWidgetRegister* widget_register, ElaraWidgetHandle widget_handle);
    virtual ~ElaraWidget();

    virtual ElaraWidgetHandle getHandle() const;

    virtual void addListener(Ref<WidgetListener> listener);
    virtual void removeListener(Ref<WidgetListener> listener);
    virtual void clearListeners();

    virtual void setParent(ElaraWidget* widget_parent);
    virtual ElaraWidget* getParent() const;
    virtual double getAbsoluteX() const;
    virtual double getAbsoluteY() const;

    virtual void setMargin(double left, double top, double right, double bottom);
    virtual void setPadding(double left, double top, double right, double bottom);

    virtual void setVisible(bool is_visible);
    virtual bool isVisible() const;
    virtual void setForegroundColorOverride(const ElaraColor& color);
    virtual void clearForegroundColorOverride();

    virtual double getMarginLeft() const;
    virtual double getMarginTop() const;
    virtual double getMarginRight() const;
    virtual double getMarginBottom() const;

    virtual double getPaddingLeft() const;
    virtual double getPaddingTop() const;
    virtual double getPaddingRight() const;
    virtual double getPaddingBottom() const;

    virtual double getContentX() const;
    virtual double getContentY() const;
    virtual double getContentWidth() const;
    virtual double getContentHeight() const;

    virtual void addChild(Ref<ElaraWidget> child);
    virtual void clearChildren();
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
    virtual double getX() const;
    virtual double getY() const;
    virtual double getWidth() const;
    virtual double getHeight() const;
    bool contains(double px, double py) const;
    bool containsLocal(double px, double py) const;

    virtual bool eventPropagate(ElaraUiEvent event);
    virtual bool handleEvent(const ElaraUiEvent& event);
    virtual bool dispatchAccelerator(unsigned int keyval, unsigned int modifiers);
    virtual bool performAction(const String& action);
    virtual void setFocused(bool focused) {}
    virtual ElaraMouseCursor cursor() const;
    virtual ElaraMouseCursor cursorAt(double px, double py) const;
    virtual bool acceptsDoubleClick() const;
    virtual bool acceptsDoubleClickAt(double px, double py) const;

    virtual void onDraw(ElaraDrawContext* ctx, int draw_width, int draw_height);
    virtual void draw(ElaraDrawContext* ctx) = 0;

    virtual void onMouseMove(double px, double py) {}
    virtual void onMouseDown(int button, double px, double py) {}
    virtual void onMouseUp(int button, double px, double py) {}
    virtual void onMouseDoubleClick(int button, double px, double py) {}
    virtual void onMouseScroll(double dx, double dy) {}
    virtual void onKeyDown(unsigned int keyval) {}
    virtual void onKeyDown(unsigned int keyval, unsigned int modifiers) {
        (void)modifiers;
        onKeyDown(keyval);
    }
    virtual void onKeyUp(unsigned int keyval) {}
    virtual void onKeyUp(unsigned int keyval, unsigned int modifiers) {
        (void)modifiers;
        onKeyUp(keyval);
    }
    virtual void onKeysTyped(const String& text) {}
};

}

#endif

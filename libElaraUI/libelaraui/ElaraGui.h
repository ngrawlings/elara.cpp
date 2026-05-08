#ifndef ELARA_GUI_H
#define ELARA_GUI_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Ref.h>

namespace elara {

enum ElaraKeyModifier {
    ELARA_KEY_MOD_SHIFT = 1 << 0,
    ELARA_KEY_MOD_CTRL = 1 << 1,
    ELARA_KEY_MOD_ALT = 1 << 2,
    ELARA_KEY_MOD_META = 1 << 3
};

enum ElaraMouseCursor {
    ELARA_CURSOR_DEFAULT,
    ELARA_CURSOR_POINTER,
    ELARA_CURSOR_TEXT,
    ELARA_CURSOR_RESIZE_EW,
    ELARA_CURSOR_RESIZE_NS
};

class ElaraDrawContext {
public:
    virtual ~ElaraDrawContext() {}

    virtual void clear(double r, double g, double b) = 0;
    virtual void setColor(double r, double g, double b) = 0;
    virtual void fillCircle(double x, double y, double radius) = 0;
    virtual void fillRect(double x, double y, double w, double h) = 0;
    virtual void line(double x1, double y1, double x2, double y2, double width) = 0;
    virtual void drawText(double x, double y, const String& text, double size) = 0;
    virtual double measureTextWidth(const String& text, double size) = 0;

};

class ElaraDrawSurface {
public:
    virtual ~ElaraDrawSurface() {}

    virtual void onDraw(ElaraDrawContext* ctx, int width, int height) = 0;

    virtual void dispatchMouseMove(double x, double y) {}
    virtual void dispatchMouseDown(int button, double x, double y) {}
    virtual void dispatchMouseUp(int button, double x, double y) {}
    virtual void dispatchDoubleClick(int button, double x, double y) {}
    virtual bool acceptsDoubleClickAt(double x, double y) const { return false; }

    virtual void dispatchKeyDown(unsigned int keyval) {}
    virtual void dispatchKeyDown(unsigned int keyval, unsigned int modifiers) {
        (void)modifiers;
        dispatchKeyDown(keyval);
    }
    virtual void dispatchKeyUp(unsigned int keyval) {}
    virtual void dispatchKeyUp(unsigned int keyval, unsigned int modifiers) {
        (void)modifiers;
        dispatchKeyUp(keyval);
    }

    virtual void onMouseMove(double x, double y) {}
    virtual void onMouseDown(int button, double x, double y) {}
    virtual void onMouseUp(int button, double x, double y) {}

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

    virtual ElaraMouseCursor currentCursor(double x, double y) {
        (void)x;
        (void)y;
        return ELARA_CURSOR_DEFAULT;
    }
};

class ElaraGuiBackend {
public:
    virtual ~ElaraGuiBackend() {}

    virtual void createWindow(
        const String& title,
        int width,
        int height,
        Ref<ElaraDrawSurface> surface
    ) = 0;

    virtual void show() = 0;
    virtual void invalidate() = 0;
    virtual int run(int argc, char** argv) = 0;
};

class ElaraWindow {
private:
    Ref<ElaraGuiBackend> backend;
    Ref<ElaraDrawSurface> surface;

public:
    ElaraWindow(Ref<ElaraGuiBackend> window_backend)
        : backend(window_backend) {}

    void setSurface(Ref<ElaraDrawSurface> draw_surface) {
        surface = draw_surface;
    }

    void create(const String& title, int width, int height) {
        backend->createWindow(title, width, height, surface);
    }

    void show() {
        backend->show();
    }

    void invalidate() {
        backend->invalidate();
    }

    int run(int argc, char** argv) {
        return backend->run(argc, argv);
    }
};

}

#endif

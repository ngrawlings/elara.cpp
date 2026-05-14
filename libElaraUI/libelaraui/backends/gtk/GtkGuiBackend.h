#ifndef ELARA_GTK_GUI_BACKEND_H
#define ELARA_GTK_GUI_BACKEND_H

#include <libelaraui/ElaraGui.h>
#include <libelaracore/memory/Array.h>

#include <gtk/gtk.h>

namespace elara {

class GtkDrawContext : public ElaraDrawContext {
private:
    cairo_t* cr;

public:
    GtkDrawContext(cairo_t* cairo_context)
        : cr(cairo_context) {}

    void clear(double r, double g, double b);
    void setColor(double r, double g, double b);
    void fillCircle(double x, double y, double radius);
    void fillRect(double x, double y, double w, double h);
    void fillRoundRect(double x, double y, double w, double h, double radius);
    void strokeRoundRect(double x, double y, double w, double h, double radius, double line_width);
    void line(double x1, double y1, double x2, double y2, double width);
    void drawText(double x, double y, const String& text, double size);
    double measureTextWidth(const String& text, double size);
    void drawBitmapRgba(
        double x,
        double y,
        int width,
        int height,
        const unsigned char* rgba,
        int stride
    );

};

class GtkGuiBackend : public ElaraGuiBackend {
private:
    class WindowState;

    GtkApplication* app;
    Array<WindowState*> windows;
    bool activated;

    void buildWindow(WindowState* state);
    void removeWindowState(WindowState* state);

    static void onActivate(GtkApplication* app, gpointer user_data);
    static void onDraw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
    static void onWindowDestroy(GtkWindow* window, gpointer user_data);

    static void onMouseMotion(
        GtkEventControllerMotion* controller,
        double x,
        double y,
        gpointer user_data
    );

    static gboolean onClickTimer(gpointer user_data);

    static void onMousePressed(
        GtkGestureClick* gesture,
        int n_press,
        double x,
        double y,
        gpointer user_data
    );

    static void onMouseReleased(
        GtkGestureClick* gesture,
        int n_press,
        double x,
        double y,
        gpointer user_data
    );

    static gboolean onKeyPressed(
        GtkEventControllerKey* controller,
        unsigned int keyval,
        unsigned int keycode,
        GdkModifierType state,
        gpointer user_data
    );

    static gboolean onKeyReleased(
        GtkEventControllerKey* controller,
        unsigned int keyval,
        unsigned int keycode,
        GdkModifierType state,
        gpointer user_data
    );

    static gboolean onMouseScrolled(
        GtkEventControllerScroll* controller,
        double dx,
        double dy,
        gpointer user_data
    );

public:
    GtkGuiBackend(const String& app_id);
    virtual ~GtkGuiBackend();

    void createWindow(
        const String& title,
        int width,
        int height,
        Ref<ElaraDrawSurface> surface
    );

    void quit();
    void show();
    void showSurface(Ref<ElaraDrawSurface> surface);
    void invalidate();
    int run(int argc, char** argv);
    void destroyWindow(Ref<ElaraDrawSurface> surface);
    void setWindowTitle(const String& title);
    void setDefaultWindowSize(int w, int h);
    void setMinimumSize(int w, int h);
    void setWindowDecorated(bool decorated);
    void minimizeWindow();
    void closeWindow();
    void beginWindowMove(int button, double x, double y);
    bool isWindowMaximized() const;
    void setWindowMaximized(bool maximized);
    void setClipboardText(const String& text);
    String getClipboardText();
};

}

#endif

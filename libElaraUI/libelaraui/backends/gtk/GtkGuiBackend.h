#ifndef ELARA_GTK_GUI_BACKEND_H
#define ELARA_GTK_GUI_BACKEND_H

#include <libelaraui/ElaraGui.h>

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
    void line(double x1, double y1, double x2, double y2, double width);
    void drawText(double x, double y, const String& text, double size);

};

class GtkGuiBackend : public ElaraGuiBackend {
private:
    GtkApplication* app;
    ::GtkWindow* window;
    GtkWidget* drawing_area;

    Ref<ElaraDrawSurface> surface;

    String pending_title;
    int pending_width;
    int pending_height;

    void buildWindow();

    static void onActivate(GtkApplication* app, gpointer user_data);
    static void onDraw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);

    static void onMouseMotion(
        GtkEventControllerMotion* controller,
        double x,
        double y,
        gpointer user_data
    );

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

public:
    GtkGuiBackend(const String& app_id);
    virtual ~GtkGuiBackend();

    void createWindow(
        const String& title,
        int width,
        int height,
        Ref<ElaraDrawSurface> surface
    );

    void show();
    void invalidate();
    int run(int argc, char** argv);
};

}

#endif

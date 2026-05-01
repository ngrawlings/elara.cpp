#include "GtkGuiBackend.h"

namespace elara {

void GtkDrawContext::clear(double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
}

void GtkDrawContext::setColor(double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
}

void GtkDrawContext::fillCircle(double x, double y, double radius) {
    cairo_arc(cr, x, y, radius, 0, 6.283185307179586);
    cairo_fill(cr);
}

void GtkDrawContext::fillRect(double x, double y, double w, double h) {
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void GtkDrawContext::line(double x1, double y1, double x2, double y2, double width) {
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

void GtkDrawContext::drawText(double x, double y, const String& text, double size) {
    cairo_select_font_face(
        cr,
        "Sans",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL
    );

    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, (const char*)text);
}

GtkGuiBackend::GtkGuiBackend(const String& app_id)
    : app(0),
      window(0),
      drawing_area(0) {
    app = gtk_application_new((const char*)app_id, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(GtkGuiBackend::onActivate), this);
}

GtkGuiBackend::~GtkGuiBackend() {
    if(app) {
        g_object_unref(app);
        app = 0;
    }
}

void GtkGuiBackend::createWindow(
    const String& title,
    int width,
    int height,
    Ref<ElaraDrawSurface> draw_surface
) {
    pending_title = title;
    pending_width = width;
    pending_height = height;
    surface = draw_surface;
}

void GtkGuiBackend::buildWindow() {
    if(window) {
        return;
    }

    window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(window, (const char*)pending_title);
    gtk_window_set_default_size(window, pending_width, pending_height);

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_focusable(drawing_area, true);

    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(drawing_area),
        GtkGuiBackend::onDraw,
        this,
        0
    );

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(GtkGuiBackend::onMouseMotion), this);
    gtk_widget_add_controller(drawing_area, motion);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(GtkGuiBackend::onMousePressed), this);
    g_signal_connect(click, "released", G_CALLBACK(GtkGuiBackend::onMouseReleased), this);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(GtkGuiBackend::onKeyPressed), this);
    g_signal_connect(key, "key-released", G_CALLBACK(GtkGuiBackend::onKeyReleased), this);
    gtk_widget_add_controller(drawing_area, key);

    gtk_window_set_child(window, drawing_area);
}

void GtkGuiBackend::show() {
    if(window) {
        gtk_window_present(window);
    }

    if(drawing_area) {
        gtk_widget_grab_focus(drawing_area);
    }
}

void GtkGuiBackend::invalidate() {
    if(drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}

int GtkGuiBackend::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app), argc, argv);
}

void GtkGuiBackend::onActivate(GtkApplication* app, gpointer user_data) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    self->buildWindow();
    self->show();
}

void GtkGuiBackend::onDraw(
    GtkDrawingArea* area,
    cairo_t* cr,
    int width,
    int height,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        GtkDrawContext ctx(cr);
        self->surface->onDraw(&ctx, width, height);
    }
}

void GtkGuiBackend::onMouseMotion(
    GtkEventControllerMotion* controller,
    double x,
    double y,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        self->surface->onMouseMove(x, y);
        self->invalidate();
    }
}

void GtkGuiBackend::onMousePressed(
    GtkGestureClick* gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        self->surface->onMouseDown(button, x, y);
    }

    if(self->drawing_area) {
        gtk_widget_grab_focus(self->drawing_area);
    }

    self->invalidate();
}

void GtkGuiBackend::onMouseReleased(
    GtkGestureClick* gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        self->surface->onMouseUp(button, x, y);
    }

    self->invalidate();
}

gboolean GtkGuiBackend::onKeyPressed(
    GtkEventControllerKey* controller,
    unsigned int keyval,
    unsigned int keycode,
    GdkModifierType state,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        self->surface->onKeyDown(keyval);
    }

    self->invalidate();

    return true;
}

gboolean GtkGuiBackend::onKeyReleased(
    GtkEventControllerKey* controller,
    unsigned int keyval,
    unsigned int keycode,
    GdkModifierType state,
    gpointer user_data
) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;

    if(self->surface) {
        self->surface->onKeyUp(keyval);
    }

    self->invalidate();

    return true;
}

}

#include "GtkGuiBackend.h"

namespace elara {

namespace {

unsigned int translateModifiers(GdkModifierType state) {
    unsigned int modifiers = 0;

    if((state & GDK_SHIFT_MASK) != 0) {
        modifiers |= ELARA_KEY_MOD_SHIFT;
    }

    if((state & GDK_CONTROL_MASK) != 0) {
        modifiers |= ELARA_KEY_MOD_CTRL;
    }

    if((state & GDK_ALT_MASK) != 0) {
        modifiers |= ELARA_KEY_MOD_ALT;
    }

    if((state & GDK_SUPER_MASK) != 0 || (state & GDK_META_MASK) != 0) {
        modifiers |= ELARA_KEY_MOD_META;
    }

    return modifiers;
}

PangoLayout* createLayout(cairo_t* cr, const String& text, double size) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_new();

    pango_font_description_set_family(desc, "Sans");
    pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, (const char*)text, -1);

    pango_font_description_free(desc);
    return layout;
}

const char* cursorName(ElaraMouseCursor cursor) {
    switch(cursor) {
        case ELARA_CURSOR_POINTER:
            return "pointer";
        case ELARA_CURSOR_TEXT:
            return "text";
        case ELARA_CURSOR_RESIZE_EW:
            return "ew-resize";
        case ELARA_CURSOR_RESIZE_NS:
            return "ns-resize";
        case ELARA_CURSOR_DEFAULT:
        default:
            return "default";
    }
}

}

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
    PangoLayout* layout = createLayout(cr, text, size);
    PangoLayoutLine* line = pango_layout_get_line_readonly(layout, 0);

    cairo_move_to(cr, x, y);
    pango_cairo_show_layout_line(cr, line);

    g_object_unref(layout);
}

double GtkDrawContext::measureTextWidth(const String& text, double size) {
    PangoLayout* layout = createLayout(cr, text, size);
    PangoRectangle logical;
    double width = 0;

    pango_layout_get_pixel_extents(layout, 0, &logical);
    width = logical.width;

    g_object_unref(layout);
    return width;
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
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
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
        self->surface->dispatchMouseMove(x, y);
        gtk_widget_set_cursor_from_name(self->drawing_area, cursorName(self->surface->currentCursor(x, y)));
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
        self->surface->dispatchMouseDown(button, x, y);
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
        self->surface->dispatchMouseUp(button, x, y);
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
        self->surface->dispatchKeyDown(keyval, translateModifiers(state));
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
        self->surface->dispatchKeyUp(keyval, translateModifiers(state));
    }

    self->invalidate();

    return true;
}

}

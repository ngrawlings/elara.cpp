#include "GtkGuiBackend.h"
#include "../../frontend/widgets/ElaraRootWidget.h"

#include <math.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <gdk/x11/gdkx.h>

namespace elara {

class GtkGuiBackend::WindowState {
public:
    GtkGuiBackend* backend;
    ::GtkWindow* window;
    GtkWidget* drawing_area;
    Ref<ElaraDrawSurface> surface;
    String title;
    int width;
    int height;
    bool decorated;

    bool visible;
    bool mouse_captured;
    bool capture_on_click;
    bool ignore_next_capture_motion;
    int capture_ignore_events_remaining;
    double last_mouse_x;
    double last_mouse_y;
    double capture_ref_x;
    double capture_ref_y;
    int pending_button;
    double pending_press_x;
    double pending_press_y;
    double pending_release_x;
    double pending_release_y;
    guint pending_timer_id;

    WindowState(
        GtkGuiBackend* window_backend,
        const String& window_title,
        int window_width,
        int window_height,
        Ref<ElaraDrawSurface> draw_surface
    ) : backend(window_backend),
        window(0),
        drawing_area(0),
        surface(draw_surface),
        title(window_title),
        width(window_width),
        height(window_height),
        decorated(true),
        visible(false),
        mouse_captured(false),
        capture_on_click(false),
        ignore_next_capture_motion(false),
        capture_ignore_events_remaining(0),
        last_mouse_x(0),
        last_mouse_y(0),
        capture_ref_x(0),
        capture_ref_y(0),
        pending_button(-1),
        pending_press_x(0),
        pending_press_y(0),
        pending_release_x(0),
        pending_release_y(0),
        pending_timer_id(0) {
    }
};

namespace {

class ClipboardReadState {
public:
    bool done;
    String text;

    ClipboardReadState()
        : done(false),
          text("") {}
};

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

static const int WINDOW_RESIZE_BORDER = 8;

static GdkSurfaceEdge windowResizeEdge(double x, double y, int w, int h) {
    bool left   = x < WINDOW_RESIZE_BORDER;
    bool right  = x >= w - WINDOW_RESIZE_BORDER;
    bool top    = y < WINDOW_RESIZE_BORDER;
    bool bottom = y >= h - WINDOW_RESIZE_BORDER;
    if(top && left)     return GDK_SURFACE_EDGE_NORTH_WEST;
    if(top && right)    return GDK_SURFACE_EDGE_NORTH_EAST;
    if(bottom && left)  return GDK_SURFACE_EDGE_SOUTH_WEST;
    if(bottom && right) return GDK_SURFACE_EDGE_SOUTH_EAST;
    if(top)    return GDK_SURFACE_EDGE_NORTH;
    if(bottom) return GDK_SURFACE_EDGE_SOUTH;
    if(left)   return GDK_SURFACE_EDGE_WEST;
    if(right)  return GDK_SURFACE_EDGE_EAST;
    return (GdkSurfaceEdge)-1;
}

static const char* resizeEdgeCursor(GdkSurfaceEdge edge) {
    switch(edge) {
        case GDK_SURFACE_EDGE_NORTH:      return "n-resize";
        case GDK_SURFACE_EDGE_SOUTH:      return "s-resize";
        case GDK_SURFACE_EDGE_WEST:       return "w-resize";
        case GDK_SURFACE_EDGE_EAST:       return "e-resize";
        case GDK_SURFACE_EDGE_NORTH_WEST: return "nw-resize";
        case GDK_SURFACE_EDGE_NORTH_EAST: return "ne-resize";
        case GDK_SURFACE_EDGE_SOUTH_WEST: return "sw-resize";
        case GDK_SURFACE_EDGE_SOUTH_EAST: return "se-resize";
        default: return "default";
    }
}

void onClipboardTextRead(GObject* source_object, GAsyncResult* result, gpointer user_data) {
    ClipboardReadState* state = (ClipboardReadState*)user_data;
    GdkClipboard* clipboard = GDK_CLIPBOARD(source_object);
    GError* error = 0;
    char* text = gdk_clipboard_read_text_finish(clipboard, result, &error);

    if(text) {
        state->text = String(text);
        g_free(text);
    }

    if(error) {
        g_error_free(error);
    }

    state->done = true;
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

static void cairo_rounded_rect_path(cairo_t* cr, double x, double y, double w, double h, double r) {
    if(r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    double max_r = (w < h ? w : h) / 2.0;
    if(r > max_r) r = max_r;
    cairo_new_path(cr);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,         3.0 * M_PI / 2.0);
    cairo_arc(cr, x + w - r, y + r,     r, 3.0 * M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,          M_PI / 2.0);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI / 2.0,   M_PI);
    cairo_close_path(cr);
}

void GtkDrawContext::fillRoundRect(double x, double y, double w, double h, double radius) {
    cairo_rounded_rect_path(cr, x, y, w, h, radius);
    cairo_fill(cr);
}

void GtkDrawContext::strokeRoundRect(double x, double y, double w, double h, double radius, double line_width) {
    cairo_set_line_width(cr, line_width);
    cairo_rounded_rect_path(cr, x, y, w, h, radius);
    cairo_stroke(cr);
}

void GtkDrawContext::line(double x1, double y1, double x2, double y2, double width) {
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

void GtkDrawContext::drawText(double x, double y, const String& text, double size) {
    if (text.byteLength() == 0 || size == 0)
        return;
        
    PangoLayout* layout = createLayout(cr, text, size);
    PangoLayoutLine* line = pango_layout_get_line_readonly(layout, 0);

    cairo_move_to(cr, x, y);
    pango_cairo_show_layout_line(cr, line);

    g_object_unref(layout);
}

double GtkDrawContext::measureTextWidth(const String& text, double size) {
    if (text.byteLength() == 0 || size == 0)
        return 0;

    PangoLayout* layout = createLayout(cr, text, size);
    PangoRectangle logical;
    double width = 0;

    pango_layout_get_pixel_extents(layout, 0, &logical);
    width = logical.width;

    g_object_unref(layout);
    return width;
}

void GtkDrawContext::drawBitmapRgba(
    double x,
    double y,
    int image_width,
    int image_height,
    const unsigned char* rgba,
    int stride
) {
    if(!rgba || image_width <= 0 || image_height <= 0 || stride <= 0) {
        return;
    }

    int cairo_stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, image_width);
    unsigned char* argb = (unsigned char*)malloc((size_t)cairo_stride * (size_t)image_height);
    if(!argb) {
        return;
    }

    for(int row = 0; row < image_height; row++) {
        const unsigned char* src = rgba + (size_t)row * (size_t)stride;
        unsigned char* dst = argb + (size_t)row * (size_t)cairo_stride;

        for(int col = 0; col < image_width; col++) {
            unsigned char r = src[col * 4 + 0];
            unsigned char g = src[col * 4 + 1];
            unsigned char b = src[col * 4 + 2];
            unsigned char a = src[col * 4 + 3];

            dst[col * 4 + 0] = b;
            dst[col * 4 + 1] = g;
            dst[col * 4 + 2] = r;
            dst[col * 4 + 3] = a;
        }
    }

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        argb,
        CAIRO_FORMAT_ARGB32,
        image_width,
        image_height,
        cairo_stride
    );

    cairo_save(cr);
    cairo_set_source_surface(cr, surface, x, y);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(surface);
    free(argb);
}

GtkGuiBackend::GtkGuiBackend(const String& app_id)
    : app(0),
      activated(false) {
    app = gtk_application_new((const char*)app_id, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(GtkGuiBackend::onActivate), this);
}

GtkGuiBackend::~GtkGuiBackend() {
    for(int i = 0; i < (int)windows.length(); i++) {
        delete windows[i];
    }
    windows.clear();

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
    WindowState* state = new WindowState(this, title, width, height, draw_surface);
    windows.push(state);

    if(activated) {
        buildWindow(state);
    }
}

void GtkGuiBackend::buildWindow(WindowState* state) {
    if(!state || state->window) {
        return;
    }

    state->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(state->window, (const char*)state->title);
    gtk_window_set_default_size(state->window, state->width, state->height);
    gtk_window_set_decorated(state->window, state->decorated ? TRUE : FALSE);

    state->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_focusable(state->drawing_area, true);

    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(state->drawing_area),
        GtkGuiBackend::onDraw,
        state,
        0
    );

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(GtkGuiBackend::onMouseMotion), state);
    gtk_widget_add_controller(state->drawing_area, motion);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(GtkGuiBackend::onMousePressed), state);
    g_signal_connect(click, "released", G_CALLBACK(GtkGuiBackend::onMouseReleased), state);
    gtk_widget_add_controller(state->drawing_area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(GtkGuiBackend::onKeyPressed), state);
    g_signal_connect(key, "key-released", G_CALLBACK(GtkGuiBackend::onKeyReleased), state);
    gtk_widget_add_controller(state->drawing_area, key);

    GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(GtkGuiBackend::onMouseScrolled), state);
    gtk_widget_add_controller(state->drawing_area, scroll);

    g_signal_connect(state->window, "destroy", G_CALLBACK(GtkGuiBackend::onWindowDestroy), state);
    gtk_window_set_child(state->window, state->drawing_area);
}

void GtkGuiBackend::quit() {
    if(app) {
        g_application_quit(G_APPLICATION(app));
    }
}

void GtkGuiBackend::show() {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(!state || !state->window || !state->visible) {
            continue;
        }

        gtk_window_present(state->window);

        if(state->drawing_area) {
            gtk_widget_grab_focus(state->drawing_area);
        }
    }
}

void GtkGuiBackend::showSurface(Ref<ElaraDrawSurface> surface) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(!state || !surface || state->surface.getPtr() != surface.getPtr()) {
            continue;
        }
        state->visible = true;
        if(state->window) {
            gtk_window_present(state->window);
            if(state->drawing_area) {
                gtk_widget_grab_focus(state->drawing_area);
            }
        }
        return;
    }
}

gboolean GtkGuiBackend::invalidateOnMainThread(gpointer user_data) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;
    if(!self) {
        return G_SOURCE_REMOVE;
    }

    for(int i = 0; i < (int)self->windows.length(); i++) {
        WindowState* state = self->windows[i];
        if(state && state->drawing_area) {
            gtk_widget_queue_draw(state->drawing_area);
        }
    }

    return G_SOURCE_REMOVE;
}

void GtkGuiBackend::invalidate() {
    g_idle_add(&GtkGuiBackend::invalidateOnMainThread, this);
}

int GtkGuiBackend::run(int argc, char** argv) {
    return g_application_run(G_APPLICATION(app), argc, argv);
}

gboolean GtkGuiBackend::onMouseScrolled(
    GtkEventControllerScroll* controller,
    double dx,
    double dy,
    gpointer user_data
) {
    WindowState* state = (WindowState*)user_data;
    if(state && state->surface) {
        double event_x = state->last_mouse_x;
        double event_y = state->last_mouse_y;
        GdkEvent* current = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
        if(current) {
            double x = 0.0;
            double y = 0.0;
            if(gdk_event_get_position(current, &x, &y)) {
                event_x = x;
                event_y = y;
                state->last_mouse_x = x;
                state->last_mouse_y = y;
            }
        }
        state->surface->dispatchMouseScroll(dx, dy, event_x, event_y);
        if(state->backend) {
            state->backend->invalidate();
        }
    }
    return TRUE;
}

void GtkGuiBackend::onActivate(GtkApplication* app, gpointer user_data) {
    GtkGuiBackend* self = (GtkGuiBackend*)user_data;
    self->activated = true;

    for(int i = 0; i < (int)self->windows.length(); i++) {
        self->buildWindow(self->windows[i]);
    }
}

void GtkGuiBackend::onDraw(
    GtkDrawingArea* area,
    cairo_t* cr,
    int width,
    int height,
    gpointer user_data
) {
    WindowState* state = (WindowState*)user_data;

    if(state && state->surface) {
        GtkDrawContext ctx(cr);
        state->surface->onDraw(&ctx, width, height);
    }
}

void GtkGuiBackend::onWindowDestroy(GtkWindow* window, gpointer user_data) {
    (void)window;
    WindowState* state = (WindowState*)user_data;

    if(state && state->backend) {
        state->backend->removeWindowState(state);
    }
}

void GtkGuiBackend::recenterPointer(WindowState* state) {
    if(!state || !state->drawing_area || !state->window) {
        return;
    }

    int width = gtk_widget_get_width(state->drawing_area);
    int height = gtk_widget_get_height(state->drawing_area);
    if(width <= 0 || height <= 0) {
        return;
    }

    double center_x = (double)width * 0.5;
    double center_y = (double)height * 0.5;
    state->capture_ref_x = center_x;
    state->capture_ref_y = center_y;
    state->last_mouse_x = center_x;
    state->last_mouse_y = center_y;

    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(state->window));
    if(!surface) {
        return;
    }
    GdkDisplay* display = gdk_surface_get_display(surface);
    if(!display || !GDK_IS_X11_DISPLAY(display)) {
        return;
    }

    Display* xdisplay = gdk_x11_display_get_xdisplay(display);
    Window xid = gdk_x11_surface_get_xid(surface);
    if(!xdisplay || xid == 0) {
        return;
    }

    state->ignore_next_capture_motion = true;
    state->capture_ignore_events_remaining = 3;
    XWarpPointer(xdisplay, None, xid, 0, 0, 0, 0, (int)center_x, (int)center_y);
    XFlush(xdisplay);
}

void GtkGuiBackend::onMouseMotion(
    GtkEventControllerMotion* controller,
    double x,
    double y,
    gpointer user_data
) {
    WindowState* state = (WindowState*)user_data;

    if(state && state->surface) {
        state->last_mouse_x = x;
        state->last_mouse_y = y;
        if(state->mouse_captured) {
            double dx = x - state->capture_ref_x;
            double dy = y - state->capture_ref_y;
            if(state->capture_ignore_events_remaining > 0 &&
               fabs(dx) <= 1.0 && fabs(dy) <= 1.0) {
                state->capture_ignore_events_remaining--;
                state->ignore_next_capture_motion = false;
                return;
            }
            if(state->ignore_next_capture_motion &&
               fabs(dx) <= 1.0 && fabs(dy) <= 1.0) {
                state->ignore_next_capture_motion = false;
                return;
            }
            if(fabs(dx) >= 4.0 || fabs(dy) >= 4.0) {
                if(dx > 64.0) { dx = 64.0; }
                if(dx < -64.0) { dx = -64.0; }
                if(dy > 64.0) { dy = 64.0; }
                if(dy < -64.0) { dy = -64.0; }
                state->surface->dispatchMouseMove(dx, dy);
                recenterPointer(state);
            }
        } else {
            state->surface->dispatchMouseMove(x, y);
            const char* cursor = cursorName(state->surface->currentCursor(x, y));
            if(!state->decorated) {
                int w = gtk_widget_get_width(state->drawing_area);
                int h = gtk_widget_get_height(state->drawing_area);
                GdkSurfaceEdge edge = windowResizeEdge(x, y, w, h);
                if(edge != (GdkSurfaceEdge)-1) {
                    cursor = resizeEdgeCursor(edge);
                }
            }
            gtk_widget_set_cursor_from_name(state->drawing_area, cursor);
            state->backend->invalidate();
        }
    }
}

gboolean GtkGuiBackend::onClickTimer(gpointer user_data) {
    WindowState* state = (WindowState*)user_data;
    state->pending_timer_id = 0;

    if(state->surface && state->pending_button >= 0) {
        state->surface->dispatchMouseDown(state->pending_button, state->pending_press_x, state->pending_press_y);
        state->surface->dispatchMouseUp(state->pending_button, state->pending_release_x, state->pending_release_y);
    }

    state->pending_button = -1;

    if(state->backend) {
        state->backend->invalidate();
    }

    return G_SOURCE_REMOVE;
}

void GtkGuiBackend::onMousePressed(
    GtkGestureClick* gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data
) {
    WindowState* state = (WindowState*)user_data;

    if(state && state->surface) {
        state->last_mouse_x = x;
        state->last_mouse_y = y;
        int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

        if(!state->decorated && button == 1) {
            int w = gtk_widget_get_width(state->drawing_area);
            int h = gtk_widget_get_height(state->drawing_area);
            GdkSurfaceEdge edge = windowResizeEdge(x, y, w, h);
            if(edge != (GdkSurfaceEdge)-1) {
                GdkSurface* gdk_surface = gtk_native_get_surface(GTK_NATIVE(state->window));
                if(gdk_surface) {
                    GdkSeat* seat = gdk_display_get_default_seat(gdk_surface_get_display(gdk_surface));
                    if(seat) {
                        gdk_toplevel_begin_resize(
                            GDK_TOPLEVEL(gdk_surface),
                            edge,
                            gdk_seat_get_pointer(seat),
                            button,
                            x,
                            y,
                            GDK_CURRENT_TIME
                        );
                    }
                }
                if(state->backend) { state->backend->invalidate(); }
                return;
            }
        }

        bool is_double = (n_press == 2) || (state->pending_timer_id != 0);

        if(state->pending_timer_id != 0) {
            g_source_remove(state->pending_timer_id);
            state->pending_timer_id = 0;
        }

        if(is_double) {
            state->pending_button = -1;
            state->surface->dispatchDoubleClick(button, x, y);
        } else if(state->surface->acceptsDoubleClickAt(x, y)) {
            state->pending_button = button;
            state->pending_press_x = x;
            state->pending_press_y = y;
            state->pending_release_x = x;
            state->pending_release_y = y;
        } else {
            state->pending_button = -1;
            state->surface->dispatchMouseDown(button, x, y);
        }
    }

    if(state && state->drawing_area) {
        gtk_widget_grab_focus(state->drawing_area);
    }

    if(state && state->backend) {
        state->backend->invalidate();
    }
}

void GtkGuiBackend::onMouseReleased(
    GtkGestureClick* gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data
) {
    WindowState* state = (WindowState*)user_data;

    if(state && state->surface) {
        state->last_mouse_x = x;
        state->last_mouse_y = y;
        int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

        if(n_press == 2 && state->pending_button >= 0) {
            if(state->pending_timer_id != 0) {
                g_source_remove(state->pending_timer_id);
                state->pending_timer_id = 0;
            }
            state->pending_button = -1;
            state->surface->dispatchDoubleClick(button, x, y);
        } else if(state->pending_button >= 0 && state->pending_timer_id == 0) {
            state->pending_release_x = x;
            state->pending_release_y = y;
            state->pending_timer_id = g_timeout_add(300, onClickTimer, state);
        } else if(state->pending_button < 0) {
            state->surface->dispatchMouseUp(button, x, y);
        }
    }

    if(state && state->backend) {
        state->backend->invalidate();
    }
}

gboolean GtkGuiBackend::onKeyPressed(
    GtkEventControllerKey* controller,
    unsigned int keyval,
    unsigned int keycode,
    GdkModifierType state,
    gpointer user_data
) {
    WindowState* window_state = (WindowState*)user_data;

    if(window_state && window_state->mouse_captured && keyval == GDK_KEY_Escape) {
        window_state->mouse_captured = false;
        gtk_widget_set_cursor_from_name(window_state->drawing_area, "default");
    }

    if(window_state && window_state->surface) {
        window_state->surface->dispatchKeyDown(keyval, translateModifiers(state));
    }

    if(window_state && window_state->backend) {
        window_state->backend->invalidate();
    }

    return true;
}

gboolean GtkGuiBackend::onKeyReleased(
    GtkEventControllerKey* controller,
    unsigned int keyval,
    unsigned int keycode,
    GdkModifierType state,
    gpointer user_data
) {
    WindowState* window_state = (WindowState*)user_data;

    if(window_state && window_state->surface) {
        window_state->surface->dispatchKeyUp(keyval, translateModifiers(state));
    }

    if(window_state && window_state->backend) {
        window_state->backend->invalidate();
    }

    return true;
}

void GtkGuiBackend::removeWindowState(WindowState* state) {
    if(!state) {
        return;
    }

    state->window = 0;
    state->drawing_area = 0;

    for(int i = 0; i < (int)windows.length(); i++) {
        if(windows[i] == state) {
            windows.remove(i);
            break;
        }
    }

    Ref<ElaraDrawSurface> surface = state->surface;
    if(surface) {
        ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(surface.getPtr());
        if(root) {
            root->sweepRegistry();
        }
    }

    delete state;

    if(activated && windows.length() <= 0 && app) {
        g_application_quit(G_APPLICATION(app));
    }
}

void GtkGuiBackend::setWindowTitle(const String& title) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            gtk_window_set_title(state->window, (const char*)title);
            return;
        }
    }
}

void GtkGuiBackend::setDefaultWindowSize(int w, int h) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            gtk_window_set_default_size(state->window, w, h);
            return;
        }
    }
}

void GtkGuiBackend::setMinimumSize(int w, int h) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            gtk_widget_set_size_request(GTK_WIDGET(state->window), w, h);
            return;
        }
    }
}

void GtkGuiBackend::setWindowDecorated(bool decorated) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(!state) {
            continue;
        }

        state->decorated = decorated;
        if(state->window) {
            gtk_window_set_decorated(state->window, decorated ? TRUE : FALSE);
        }
        return;
    }
}

void GtkGuiBackend::minimizeWindow() {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            gtk_window_minimize(state->window);
            return;
        }
    }
}

void GtkGuiBackend::closeWindow() {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            gtk_window_destroy(state->window);
            return;
        }
    }
}

void GtkGuiBackend::beginWindowMove(int button, double x, double y) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(!state || !state->window) {
            continue;
        }

        GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(state->window));
        if(!surface) {
            return;
        }

        GdkDisplay* display = gdk_surface_get_display(surface);
        if(!display) {
            return;
        }

        GdkSeat* seat = gdk_display_get_default_seat(display);
        if(!seat) {
            return;
        }

        GdkDevice* device = gdk_seat_get_pointer(seat);
        if(!device) {
            return;
        }

        gdk_toplevel_begin_move(
            GDK_TOPLEVEL(surface),
            device,
            button > 0 ? button : 1,
            x,
            y,
            GDK_CURRENT_TIME
        );
        return;
    }
}

bool GtkGuiBackend::isWindowMaximized() const {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            return gtk_window_is_maximized(state->window);
        }
    }
    return false;
}

void GtkGuiBackend::setWindowMaximized(bool maximized) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(state && state->window) {
            if(maximized) {
                gtk_window_maximize(state->window);
            } else {
                gtk_window_unmaximize(state->window);
            }
            return;
        }
    }
}

void GtkGuiBackend::setMouseCaptured(bool captured) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];
        if(!state || !state->drawing_area) {
            continue;
        }
        state->mouse_captured = captured;
        state->capture_on_click = false;
        if(captured) {
            state->capture_ignore_events_remaining = 3;
            if(state->drawing_area) {
                gtk_widget_grab_focus(state->drawing_area);
            }
            gtk_widget_set_cursor_from_name(state->drawing_area, "none");
            recenterPointer(state);
        } else {
            state->ignore_next_capture_motion = false;
            state->capture_ignore_events_remaining = 0;
            gtk_widget_set_cursor_from_name(state->drawing_area, "default");
        }
        return;
    }
}

void GtkGuiBackend::destroyWindow(Ref<ElaraDrawSurface> surface) {
    for(int i = 0; i < (int)windows.length(); i++) {
        WindowState* state = windows[i];

        if(!state || !surface || state->surface.getPtr() != surface.getPtr()) {
            continue;
        }

        if(state->window) {
            gtk_window_destroy(state->window);
        } else {
            removeWindowState(state);
        }
        return;
    }
}

void GtkGuiBackend::setClipboardText(const String& text) {
    GdkDisplay* display = gdk_display_get_default();
    if(!display) {
        return;
    }

    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if(!clipboard) {
        return;
    }

    gdk_clipboard_set_text(clipboard, (const char*)text);
}

String GtkGuiBackend::getClipboardText() {
    GdkDisplay* display = gdk_display_get_default();
    if(!display) {
        return String();
    }

    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if(!clipboard) {
        return String();
    }

    ClipboardReadState state;
    gdk_clipboard_read_text_async(clipboard, 0, onClipboardTextRead, &state);

    while(!state.done) {
        g_main_context_iteration(0, true);
    }

    return state.text;
}

}

#include "ElaraWidget.h"
#include "../ElaraWidgetRegistry.h"
#include "ElaraRootWidget.h"

namespace elara {

class ElaraWidgetOffsetDrawContext : public ElaraDrawContext {
private:
    ElaraDrawContext* ctx;
    double offset_x;
    double offset_y;
    double clip_width;
    double clip_height;

public:
    ElaraWidgetOffsetDrawContext(
        ElaraDrawContext* draw_context,
        double x,
        double y,
        double w,
        double h
    ) : ctx(draw_context),
        offset_x(x),
        offset_y(y),
        clip_width(w),
        clip_height(h) {
        if(clip_width < 0) {
            clip_width = 0;
        }

        if(clip_height < 0) {
            clip_height = 0;
        }
    }

    double getOffsetX() const {
        return offset_x;
    }

    double getOffsetY() const {
        return offset_y;
    }

    void clear(double r, double g, double b) {
        ctx->setColor(r, g, b);
        ctx->fillRect(offset_x, offset_y, clip_width, clip_height);
    }

    void setColor(double r, double g, double b) {
        ctx->setColor(r, g, b);
    }

    void fillCircle(double x, double y, double radius) {
        ctx->fillCircle(offset_x + x, offset_y + y, radius);
    }

    void fillRect(double x, double y, double w, double h) {
        ctx->fillRect(offset_x + x, offset_y + y, w, h);
    }

    void fillRoundRect(double x, double y, double w, double h, double radius) {
        ctx->fillRoundRect(offset_x + x, offset_y + y, w, h, radius);
    }

    void strokeRoundRect(double x, double y, double w, double h, double radius, double line_width) {
        ctx->strokeRoundRect(offset_x + x, offset_y + y, w, h, radius, line_width);
    }

    void line(double x1, double y1, double x2, double y2, double line_width) {
        ctx->line(
            offset_x + x1,
            offset_y + y1,
            offset_x + x2,
            offset_y + y2,
            line_width
        );
    }

    void drawText(double x, double y, const String& text, double size) {
        ctx->drawText(offset_x + x, offset_y + y, text, size);
    }

    double measureTextWidth(const String& text, double size) {
        return ctx->measureTextWidth(text, size);
    }

    void drawBitmapRgba(
        double x,
        double y,
        int width,
        int height,
        const unsigned char* rgba,
        int stride
    ) {
        ctx->drawBitmapRgba(offset_x + x, offset_y + y, width, height, rgba, stride);
    }
};

ElaraWidget::ElaraWidget() :
    visible(true),
    hovered(false),
    mouse_down(false),
    hover_only(false),
    hovered_child_index(-1),
    x(0),
    y(0),
    width(0),
    height(0),
    margin_left(0),
    margin_top(0),
    margin_right(0),
    margin_bottom(0),
    padding_left(0),
    padding_top(0),
    padding_right(0),
    padding_bottom(0),
    z_order(0),
    parent(0),
    palette(0) {}

ElaraWidget::ElaraWidget(ElaraWidgetRegister* widget_register, ElaraWidgetHandle widget_handle)
    : widget_handle(widget_handle),
      visible(true),
      hovered(false),
      mouse_down(false),
      hover_only(false),
      hovered_child_index(-1),
      x(0),
      y(0),
      width(0),
      height(0),
      margin_left(0),
      margin_top(0),
      margin_right(0),
      margin_bottom(0),
      padding_left(0),
      padding_top(0),
      padding_right(0),
      padding_bottom(0),
      z_order(0),
      parent(0),
      palette(0) {
    widget_register->registerWidget(widget_handle, this);
}

ElaraWidget::~ElaraWidget() {}

ElaraWidgetHandle ElaraWidget::getHandle() const {
    return widget_handle;
}

void ElaraWidget::addListener(Ref<WidgetListener> listener) {
    if(listener) {
        listeners.push(listener);
    }
}

void ElaraWidget::removeListener(Ref<WidgetListener> listener) {
    if(!listener) {
        return;
    }

    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i].getPtr() == listener.getPtr()) {
            listeners.remove(i);
            return;
        }
    }
}

void ElaraWidget::clearListeners() {
    listeners.clear();
}

void ElaraWidget::emitMouseMove(double px, double py) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetMouseMove(widget_handle, px, py);
        }
    }
}

void ElaraWidget::emitMouseDown(int button, double px, double py) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetMouseDown(widget_handle, button, px, py);
        }
    }
}

void ElaraWidget::emitMouseUp(int button, double px, double py) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetMouseUp(widget_handle, button, px, py);
        }
    }
}

void ElaraWidget::emitMouseScroll(double dx, double dy) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetMouseScroll(widget_handle, dx, dy);
        }
    }
}

void ElaraWidget::emitClicked(int button, double px, double py) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetClicked(widget_handle, button, px, py);
        }
    }
}

void ElaraWidget::emitHoverChanged(bool is_hovered) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetHoverChanged(widget_handle, is_hovered);
        }
    }
}

void ElaraWidget::emitKeyDown(unsigned int keyval) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetKeyDown(widget_handle, keyval);
        }
    }
}

void ElaraWidget::emitKeyUp(unsigned int keyval) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetKeyUp(widget_handle, keyval);
        }
    }
}

void ElaraWidget::emitKeysTyped(const String& text) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetKeysTyped(widget_handle, text);
        }
    }
}

void ElaraWidget::emitTextChanged(const String& text) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetTextChanged(widget_handle, text);
        }
    }
}

void ElaraWidget::emitTextChangedWithCaret(const String& text, int caret) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetTextChangedWithCaret(widget_handle, text, caret);
        }
    }
}

void ElaraWidget::emitValueChanged(double value) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetValueChanged(widget_handle, value);
        }
    }
}

void ElaraWidget::emitAction(const String& action) {
    for(int i = 0; i < (int)listeners.length(); i++) {
        if(listeners[i]) {
            listeners[i]->onWidgetAction(widget_handle, action);
        }
    }
}

void ElaraWidget::setParent(ElaraWidget* widget_parent) {
    parent = widget_parent;
}

ElaraWidget* ElaraWidget::getParent() const {
    return parent;
}

double ElaraWidget::getAbsoluteX() const {
    if(parent) {
        return parent->getAbsoluteX() + x + margin_left;
    }

    return x + margin_left;
}

double ElaraWidget::getAbsoluteY() const {
    if(parent) {
        return parent->getAbsoluteY() + y + margin_top;
    }

    return y + margin_top;
}

void ElaraWidget::setVisible(bool is_visible) {
    visible = is_visible;
}

bool ElaraWidget::isVisible() const {
    return visible;
}

void ElaraWidget::setHoverOnly(bool hover_only_mode) {
    hover_only = hover_only_mode;
}

bool ElaraWidget::isHoverOnly() const {
    return hover_only;
}

void ElaraWidget::setForegroundColorOverride(const ElaraColor& color) {
    (void)color;
}

void ElaraWidget::clearForegroundColorOverride() {}

void ElaraWidget::setMargin(double left, double top, double right, double bottom) {
    margin_left = left;
    margin_top = top;
    margin_right = right;
    margin_bottom = bottom;
}

void ElaraWidget::setPadding(double left, double top, double right, double bottom) {
    padding_left = left;
    padding_top = top;
    padding_right = right;
    padding_bottom = bottom;
}

double ElaraWidget::getMarginLeft() const { return margin_left; }
double ElaraWidget::getMarginTop() const { return margin_top; }
double ElaraWidget::getMarginRight() const { return margin_right; }
double ElaraWidget::getMarginBottom() const { return margin_bottom; }

double ElaraWidget::getPaddingLeft() const { return padding_left; }
double ElaraWidget::getPaddingTop() const { return padding_top; }
double ElaraWidget::getPaddingRight() const { return padding_right; }
double ElaraWidget::getPaddingBottom() const { return padding_bottom; }

double ElaraWidget::getContentX() const { return padding_left; }
double ElaraWidget::getContentY() const { return padding_top; }

double ElaraWidget::getContentWidth() const {
    double result = width - padding_left - padding_right;
    return result < 0 ? 0 : result;
}

double ElaraWidget::getContentHeight() const {
    double result = height - padding_top - padding_bottom;
    return result < 0 ? 0 : result;
}

void ElaraWidget::setPalette(ElaraPalette* widget_palette) {
    palette = widget_palette;

    for(int i = 0; i < (int)children.length(); i++) {
        if(children[i]) {
            children[i]->setPalette(widget_palette);
        }
    }
}

ElaraPalette* ElaraWidget::getPalette() const {
    return palette;
}

ElaraPaletteTriplet ElaraWidget::colors(
    const String& master,
    const String& sub
) const {
    if(palette) {
        return palette->get(master, sub);
    }

    return ElaraPaletteTriplet();
}

ElaraColor ElaraWidget::base(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).base;
}

ElaraColor ElaraWidget::accent(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).accent;
}

ElaraColor ElaraWidget::text(
    const String& master,
    const String& sub
) const {
    return colors(master, sub).text;
}

void ElaraWidget::setBounds(double px, double py, double w, double h) {
    x = px;
    y = py;
    width = w;
    height = h;
}

double ElaraWidget::getX() const {
    return x;
}

double ElaraWidget::getY() const {
    return y;
}

double ElaraWidget::getWidth() const {
    return width;
}

double ElaraWidget::getHeight() const {
    return height;
}

void ElaraWidget::onDraw(
    ElaraDrawContext* ctx,
    int draw_width,
    int draw_height
) {
    width = draw_width;
    height = draw_height;

    double draw_x = x + margin_left;
    double draw_y = y + margin_top;

    /* Incoming translated contexts compose relative bounds. Raw backend contexts use absolute bounds. */
    ElaraWidgetOffsetDrawContext* parent_ctx =
        dynamic_cast<ElaraWidgetOffsetDrawContext*>(ctx);

    if(!parent_ctx) {
        draw_x = getAbsoluteX();
        draw_y = getAbsoluteY();
    }

    ElaraWidgetOffsetDrawContext local_ctx(
        ctx,
        draw_x,
        draw_y,
        width - margin_left - margin_right,
        height - margin_top - margin_bottom
    );

    draw(&local_ctx);
}

void ElaraWidget::addChild(Ref<ElaraWidget> child) {
    if(child) {
        child->setParent(this);
        child->setPalette(palette);
        children.push(child);
    }
}

void ElaraWidget::clearChildren() {
    for(int i = 0; i < (int)children.length(); i++) {
        Ref<ElaraWidget> child = children[i];

        if(!child) {
            continue;
        }

        child->clearChildren();
        child->clearListeners();

        ElaraWidget* ancestor = this;
        while(ancestor && ancestor->getParent()) {
            ancestor = ancestor->getParent();
        }

        ElaraRootWidget* root = ancestor
            ? dynamic_cast<ElaraRootWidget*>(ancestor)
            : 0;

        if(root) {
            root->onWidgetRemoved(child->getHandle());
            root->unregisterWidget(child->getHandle());
        }

        child->setParent(0);
    }

    children.clear();
}

int ElaraWidget::childCount() const {
    return (int)children.length();
}

Ref<ElaraWidget> ElaraWidget::getChild(int index) const {
    if(index < 0 || index >= (int)children.length()) {
        return Ref<ElaraWidget>(0);
    }

    return children[index];
}

void ElaraWidget::setZOrder(int z) {
    z_order = z;
}

int ElaraWidget::getZOrder() const {
    return z_order;
}

bool ElaraWidget::contains(double px, double py) const {
    return px >= x + margin_left &&
           py >= y + margin_top &&
           px <= x + width - margin_right &&
           py <= y + height - margin_bottom;
}

bool ElaraWidget::containsLocal(double px, double py) const {
    return px >= 0 && py >= 0 && px <= width && py <= height;
}

bool ElaraWidget::eventPropagate(ElaraUiEvent event) {
    if(!visible) {
        return false;
    }

    bool is_mouse =
        event.type == ELARA_UI_MOUSE_MOVE ||
        event.type == ELARA_UI_MOUSE_DOWN ||
        event.type == ELARA_UI_MOUSE_UP ||
        event.type == ELARA_UI_MOUSE_DOUBLE_CLICK ||
        event.type == ELARA_UI_MOUSE_SCROLL;

    if(is_mouse) {
        int winner = -1;
        int winner_z = -2147483647;

        for(int i = 0; i < (int)children.length(); i++) {
            if(!children[i] || !children[i]->isVisible()) {
                continue;
            }

            if(children[i]->contains(event.x, event.y)) {
                int child_z = children[i]->getZOrder();

                if(winner < 0 || child_z >= winner_z) {
                    winner = i;
                    winner_z = child_z;
                }
            }
        }

        if(event.type == ELARA_UI_MOUSE_MOVE && hovered_child_index >= 0 && hovered_child_index != winner) {
            if(hovered_child_index < (int)children.length()) {
                Ref<ElaraWidget> previous_child = children[hovered_child_index];
                if(previous_child && previous_child->isVisible()) {
                    ElaraUiEvent leave_event = event;
                    leave_event.x = -1.0;
                    leave_event.y = -1.0;
                    previous_child->eventPropagate(leave_event);
                }
            }
            hovered_child_index = -1;
        }

        if(winner >= 0) {
            ElaraUiEvent child_event = event;
            child_event.x = event.x - children[winner]->getX() - children[winner]->getMarginLeft();
            child_event.y = event.y - children[winner]->getY() - children[winner]->getMarginTop();
            if(event.type == ELARA_UI_MOUSE_MOVE) {
                hovered_child_index = winner;
            }

            return children[winner]->eventPropagate(child_event);
        }

        if(event.type == ELARA_UI_MOUSE_MOVE) {
            hovered_child_index = -1;
        }
    }

    return handleEvent(event);
}

bool ElaraWidget::handleEvent(const ElaraUiEvent& event) {
    switch(event.type) {
        case ELARA_UI_MOUSE_MOVE:
            onMouseMove(event.x, event.y);
            return true;

        case ELARA_UI_MOUSE_DOWN:
            onMouseDown(event.button, event.x, event.y);
            return true;

        case ELARA_UI_MOUSE_UP:
            onMouseUp(event.button, event.x, event.y);
            return true;

        case ELARA_UI_MOUSE_DOUBLE_CLICK:
            onMouseDoubleClick(event.button, event.x, event.y);
            return true;

        case ELARA_UI_MOUSE_SCROLL:
            onMouseScroll(event.scroll_dx, event.scroll_dy);
            return true;

        case ELARA_UI_KEY_DOWN:
            onKeyDown(event.keyval);
            return true;

        case ELARA_UI_KEY_UP:
            onKeyUp(event.keyval);
            return true;
    }

    return false;
}

bool ElaraWidget::dispatchAccelerator(unsigned int keyval, unsigned int modifiers) {
    (void)keyval;
    (void)modifiers;

    for(int i = 0; i < (int)children.length(); i++) {
        if(children[i] && children[i]->isVisible() && children[i]->dispatchAccelerator(keyval, modifiers)) {
            return true;
        }
    }

    return false;
}

bool ElaraWidget::performAction(const String& action) {
    (void)action;
    return false;
}

ElaraMouseCursor ElaraWidget::cursor() const {
    return ELARA_CURSOR_DEFAULT;
}

bool ElaraWidget::acceptsDoubleClick() const {
    return false;
}

bool ElaraWidget::acceptsDoubleClickAt(double px, double py) const {
    for(int i = (int)children.length() - 1; i >= 0; i--) {
        if(!children[i] || !children[i]->isVisible()) {
            continue;
        }

        if(children[i]->contains(px, py)) {
            return children[i]->acceptsDoubleClickAt(
                px - children[i]->getX() - children[i]->getMarginLeft(),
                py - children[i]->getY() - children[i]->getMarginTop()
            );
        }
    }

    return containsLocal(px, py) ? acceptsDoubleClick() : false;
}

ElaraMouseCursor ElaraWidget::cursorAt(double px, double py) const {
    for(int i = (int)children.length() - 1; i >= 0; i--) {
        if(!children[i] || !children[i]->isVisible()) {
            continue;
        }

        if(children[i]->contains(px, py)) {
            return children[i]->cursorAt(
                px - children[i]->getX() - children[i]->getMarginLeft(),
                py - children[i]->getY() - children[i]->getMarginTop()
            );
        }
    }

    return containsLocal(px, py) ? cursor() : ELARA_CURSOR_DEFAULT;
}

}

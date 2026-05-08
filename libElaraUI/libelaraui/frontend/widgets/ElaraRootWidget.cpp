#include "ElaraRootWidget.h"
#include <assert.h>
#include "ElaraTextInputWidget.h"
#include "../ElaraWidgetStateProbe.h"

#include <libelaracore/memory/LinkedList.h>
#include <libelaravector/elara_vector.h>

namespace elara {

namespace {

bool handlesEqual(const ElaraWidgetHandle& left, const ElaraWidgetHandle& right) {
    return left.getHandle() == right.getHandle();
}

ElaraPopupWidget* asPopup(Ref<ElaraWidget> widget) {
    return widget ? dynamic_cast<ElaraPopupWidget*>(widget.getPtr()) : 0;
}

void renderVectorNode(EvNode *node, ElaraDrawContext *ctx, double tx, double ty) {
    if (!node) return;

    double nx = tx + node->transform.x;
    double ny = ty + node->transform.y;

    switch (node->type) {
        case EV_NODE_RECT:
            if (node->style.has_fill) {
                ctx->setColor(
                    node->style.fill.r / 255.0,
                    node->style.fill.g / 255.0,
                    node->style.fill.b / 255.0
                );
                ctx->fillRect(
                    nx + node->data.rect.x,
                    ny + node->data.rect.y,
                    node->data.rect.w,
                    node->data.rect.h
                );
            }
            if (node->style.has_stroke) {
                ctx->setColor(
                    node->style.stroke.r / 255.0,
                    node->style.stroke.g / 255.0,
                    node->style.stroke.b / 255.0
                );
                double rx = nx + node->data.rect.x;
                double ry = ny + node->data.rect.y;
                double rw = node->data.rect.w;
                double rh = node->data.rect.h;
                double sw = node->style.stroke_width;
                ctx->line(rx,      ry,      rx + rw, ry,      sw);
                ctx->line(rx + rw, ry,      rx + rw, ry + rh, sw);
                ctx->line(rx + rw, ry + rh, rx,      ry + rh, sw);
                ctx->line(rx,      ry + rh, rx,      ry,      sw);
            }
            break;

        case EV_NODE_CIRCLE:
            if (node->style.has_fill) {
                ctx->setColor(
                    node->style.fill.r / 255.0,
                    node->style.fill.g / 255.0,
                    node->style.fill.b / 255.0
                );
                ctx->fillCircle(
                    nx + node->data.circle.x,
                    ny + node->data.circle.y,
                    node->data.circle.r
                );
            }
            break;

        case EV_NODE_LINE:
            if (node->style.has_stroke) {
                ctx->setColor(
                    node->style.stroke.r / 255.0,
                    node->style.stroke.g / 255.0,
                    node->style.stroke.b / 255.0
                );
                ctx->line(
                    nx + node->data.line.x1,
                    ny + node->data.line.y1,
                    nx + node->data.line.x2,
                    ny + node->data.line.y2,
                    node->style.stroke_width
                );
            }
            break;

        case EV_NODE_TEXT:
            if (node->data.text.text) {
                if (node->style.has_fill) {
                    ctx->setColor(
                        node->style.fill.r / 255.0,
                        node->style.fill.g / 255.0,
                        node->style.fill.b / 255.0
                    );
                }
                ctx->drawText(
                    nx + node->data.text.x,
                    ny + node->data.text.y,
                    String(node->data.text.text),
                    node->data.text.size
                );
            }
            break;

        case EV_NODE_PATH:
            if (node->style.has_stroke) {
                ctx->setColor(
                    node->style.stroke.r / 255.0,
                    node->style.stroke.g / 255.0,
                    node->style.stroke.b / 255.0
                );
                double px = 0.0, py = 0.0;
                for (size_t i = 0; i < node->data.path.count; ++i) {
                    EvPathSeg *seg = &node->data.path.segments[i];
                    switch (seg->type) {
                        case EV_SEG_MOVE:
                            px = nx + seg->data.move.x;
                            py = ny + seg->data.move.y;
                            break;
                        case EV_SEG_LINE: {
                            double ex = nx + seg->data.line.x;
                            double ey = ny + seg->data.line.y;
                            ctx->line(px, py, ex, ey, node->style.stroke_width);
                            px = ex;
                            py = ey;
                            break;
                        }
                        default: break;
                    }
                }
            }
            break;

        default: break;
    }

    for (size_t i = 0; i < node->child_count; ++i) {
        renderVectorNode(node->children[i], ctx, nx, ny);
    }
}

void renderVectorOverlay(ElaraVectorDocument *overlay, ElaraDrawContext *ctx,
                         double root_w, double root_h) {
    EvDocument *doc = overlay->getDocument();
    if (!doc) return;

    double x = overlay->getX();
    double y = overlay->getY();

    if (overlay->getAnchorH() == ELARA_OVERLAY_ANCHOR_RIGHT)
        x = root_w - doc->width - x;

    if (overlay->getAnchorV() == ELARA_OVERLAY_ANCHOR_BOTTOM)
        y = root_h - doc->height - y;

    for (size_t i = 0; i < doc->child_count; ++i) {
        renderVectorNode(doc->children[i], ctx, x, y);
    }
}

}

ElaraRootWidget::ElaraRootWidget(const String& root_widget_id)
    : root_id(root_widget_id),
      overlay_count(0),
      max_overlays(32) {
    event_filter = Ref<WidgetListener>(new ElaraOutboundEventFilter());
}

ElaraRootWidget::~ElaraRootWidget() {
    sweepRegistry();
}

String ElaraRootWidget::getRootId() const {
    return root_id;
}

ElaraWidgetHandle ElaraRootWidget::qualifyHandle(ElaraWidgetHandle widget_handle) const {
    Memory handle_memory = widget_handle.getHandle();
    String handle((const char*)handle_memory.getPtr(), handle_memory.length());

    if(handle.length() <= 0) {
        return widget_handle;
    }

    if(handle.indexOf(String("::")) >= 0) {
        return widget_handle;
    }

    return ElaraWidgetHandle(root_id + String("::") + handle);
}

void ElaraRootWidget::unregisterWidget(ElaraWidgetHandle widget_handle) {
    ElaraWidgetRegistry::getInstance()->removeWidget(qualifyHandle(widget_handle));
}

void ElaraRootWidget::sweepRegistry() {
    dismissAllPopups();
    focus = ElaraWidgetHandle();
    content = ElaraWidgetHandle();
    ElaraWidgetRegistry::getInstance()->clearNamespace(root_id);
}

void ElaraRootWidget::setContent(ElaraWidgetHandle root_content) {
    content = root_content;

    Ref<ElaraWidget> c = getWidget(content);
    if(c.getPtr()) {
        c->setPalette(palette);
    }
}

Ref<ElaraWidget> ElaraRootWidget::getContent() const {
    return getWidget(content);
}

void ElaraRootWidget::setPopup(ElaraWidgetHandle root_popup) {
    clearPopups();
    pushPopup(root_popup);
}

void ElaraRootWidget::clearPopups() {
    popups.clear();
}

void ElaraRootWidget::dismissAllPopups() {
    for(int i = 0; i < (int)popups.length(); i++) {
        ElaraPopupWidget* popup_widget = asPopup(getWidget(popups[i]));
        if(popup_widget) {
            popup_widget->hide();
        }
    }

    popups.clear();
}

Ref<ElaraWidget> ElaraRootWidget::getPopup() const {
    return getPopup(0);
}

void ElaraRootWidget::pushPopup(ElaraWidgetHandle root_popup) {
    if(root_popup.getHandle().length() <= 0) {
        return;
    }

    removePopup(root_popup);
    popups.push(root_popup);

    Ref<ElaraWidget> popup = getWidget(root_popup);
    if(popup.getPtr()) {
        popup->setPalette(palette);
        popup->setZOrder(1000 + (int)popups.length());
    }
}

void ElaraRootWidget::removePopup(ElaraWidgetHandle root_popup) {
    for(int i = 0; i < (int)popups.length(); i++) {
        if(handlesEqual(popups[i], root_popup)) {
            popups.remove(i);
            return;
        }
    }
}

void ElaraRootWidget::dismissPopupsAfter(ElaraWidgetHandle root_popup) {
    int index = -1;

    for(int i = 0; i < (int)popups.length(); i++) {
        if(handlesEqual(popups[i], root_popup)) {
            index = i;
            break;
        }
    }

    if(index < 0) {
        dismissAllPopups();
        return;
    }

    for(int i = (int)popups.length() - 1; i > index; i--) {
        ElaraPopupWidget* popup_widget = asPopup(getWidget(popups[i]));
        if(popup_widget) {
            popup_widget->hide();
        }

        popups.remove(i);
    }
}

int ElaraRootWidget::popupCount() const {
    return (int)popups.length();
}

Ref<ElaraWidget> ElaraRootWidget::getPopup(int index) const {
    if(index < 0 || index >= (int)popups.length()) {
        return Ref<ElaraWidget>(0);
    }

    return getWidget(popups[index]);
}

void ElaraRootWidget::registerWidget(ElaraWidgetHandle widget_handle, void* widget) {
    ElaraWidgetRegistry::getInstance()->setWidget(qualifyHandle(widget_handle), (ElaraWidget*)widget);
    ((ElaraWidget*)widget)->addListener(this->event_filter);
}

void ElaraRootWidget::onWidgetRemoved(ElaraWidgetHandle widget_handle) {
    if(handlesEqual(content, widget_handle)) {
        content = ElaraWidgetHandle();
    }

    if(handlesEqual(focus, widget_handle)) {
        Ref<ElaraWidget> focused_widget = getWidget(focus);
        ElaraTextInputWidget* focused_input = focused_widget
            ? dynamic_cast<ElaraTextInputWidget*>(focused_widget.getPtr())
            : 0;

        if(focused_input) {
            focused_input->setFocused(false);
        }

        focus = ElaraWidgetHandle();
    }

    removePopup(widget_handle);
}

Ref<ElaraWidget> ElaraRootWidget::getWidget(ElaraWidgetHandle widget_handle) const {
    return ElaraWidgetRegistry::getInstance()->getWidget(qualifyHandle(widget_handle));
}

bool ElaraRootWidget::probeWidgetState(
    ElaraWidgetHandle widget_handle,
    ElaraWidgetState& state
) const {
    Ref<ElaraWidget> widget = getWidget(widget_handle);

    if(!widget) {
        state = ElaraWidgetState();
        return false;
    }

    state = ElaraWidgetStateProbe::widgetState(widget);
    return true;
}

bool ElaraRootWidget::probeWidgetSnapshot(
    ElaraWidgetHandle widget_handle,
    ElaraWidgetSnapshot& snapshot
) const {
    Ref<ElaraWidget> widget = getWidget(widget_handle);

    if(!widget) {
        snapshot = ElaraWidgetSnapshot();
        return false;
    }

    snapshot = ElaraWidgetStateProbe::widgetSnapshot(widget);
    return true;
}

void ElaraRootWidget::probeRootSnapshot(ElaraRootSnapshot& snapshot) const {
    snapshot = ElaraWidgetStateProbe::rootSnapshot((ElaraRootWidget*)this);
}

String ElaraRootWidget::getWidgetStateJson(ElaraWidgetHandle widget_handle) const {
    ElaraWidgetState state;
    if(!probeWidgetState(widget_handle, state)) {
        return "{}";
    }
    return ElaraWidgetStateProbe::widgetStateJson(state);
}

String ElaraRootWidget::getWidgetSnapshotJson(ElaraWidgetHandle widget_handle) const {
    ElaraWidgetSnapshot snapshot;
    if(!probeWidgetSnapshot(widget_handle, snapshot)) {
        return "null";
    }
    return ElaraWidgetStateProbe::widgetSnapshotJson(snapshot);
}

String ElaraRootWidget::getRootSnapshotJson() const {
    ElaraRootSnapshot snapshot;
    probeRootSnapshot(snapshot);
    return ElaraWidgetStateProbe::rootSnapshotJson(snapshot);
}

void ElaraRootWidget::setFocus(ElaraWidgetHandle widget_handle) {
    this->focus = widget_handle;
}

ElaraWidgetHandle ElaraRootWidget::getFocus() const {
    return focus;
}

void ElaraRootWidget::enableOutboundEvent(const String& action) {
    ElaraOutboundEventFilter* filter = (ElaraOutboundEventFilter*)event_filter.getPtr();

    if(filter) {
        filter->enable(action);
    }
}

void ElaraRootWidget::disableOutboundEvent(const String& action) {
    ElaraOutboundEventFilter* filter = (ElaraOutboundEventFilter*)event_filter.getPtr();

    if(filter) {
        filter->disable(action);
    }
}

void ElaraRootWidget::setPalette(ElaraPalette* widget_palette) {
    ElaraWidget::setPalette(widget_palette);

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->setPalette(widget_palette);
    }

    for(int i = 0; i < (int)popups.length(); i++) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        if(popup) {
            popup->setPalette(widget_palette);
        }
    }
}

void ElaraRootWidget::addVectorOverlay(const String& id, ElaraVectorDocument overlay) {
    String id_key = id;
    Memory key((char*)id_key, id_key.length());
    bool replacing = vector_overlays.get(key).getPtr() != 0;

    if (!replacing && max_overlays > 0) {
        assert(overlay_count < max_overlays);
    }

    vector_overlays.set(id, overlay);

    if (!replacing) {
        overlay_count++;
    }
}

void ElaraRootWidget::removeVectorOverlay(const String& id) {
    String id_key = id;
    Memory key((char*)id_key, id_key.length());
    if (vector_overlays.get(key).getPtr()) {
        vector_overlays.remove(key);
        overlay_count--;
    }
}

void ElaraRootWidget::clearVectorOverlays() {
    vector_overlays.clear();
    overlay_count = 0;
}

void ElaraRootWidget::setMaxOverlays(int max) {
    max_overlays = max;
}

int ElaraRootWidget::getMaxOverlays() const {
    return max_overlays;
}

void ElaraRootWidget::draw(ElaraDrawContext* ctx) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->setBounds(0, 0, width, height);
        c->onDraw(ctx, (int)width, (int)height);
    }

    for(int i = 0; i < (int)popups.length(); i++) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        ElaraPopupWidget* popup_widget = asPopup(popup);

        if(popup_widget && popup_widget->isVisible()) {
            popup->draw(ctx);
        }
    }

    LinkedList< Ref<ElaraVectorDocument> > overlays = vector_overlays.getObjects(Memory());
    LinkedListState< Ref<ElaraVectorDocument> > it(&overlays);
    Ref<ElaraVectorDocument> *overlay_ptr;
    while (it.iterate(&overlay_ptr)) {
        if (*overlay_ptr) {
            renderVectorOverlay(overlay_ptr->getPtr(), ctx, width, height);
        }
    }
}

ElaraMouseCursor ElaraRootWidget::currentCursor(double x, double y) {
    for(int i = (int)popups.length() - 1; i >= 0; i--) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        ElaraPopupWidget* popup_widget = asPopup(popup);

        if(!popup_widget || !popup_widget->isVisible()) {
            continue;
        }

        if(popup_widget->contains(x, y)) {
            return popup_widget->cursorAt(
                x - popup_widget->getX() - popup_widget->getMarginLeft(),
                y - popup_widget->getY() - popup_widget->getMarginTop()
            );
        }
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        return c->cursorAt(x, y);
    }

    return ELARA_CURSOR_DEFAULT;
}

bool ElaraRootWidget::eventPropagate(ElaraUiEvent event) {
    bool is_mouse =
        event.type == ELARA_UI_MOUSE_MOVE ||
        event.type == ELARA_UI_MOUSE_DOWN ||
        event.type == ELARA_UI_MOUSE_UP;

    if(!is_mouse) {
        return ElaraWidget::eventPropagate(event);
    }

    ElaraPopupWidget* topmost_visible = 0;

    for(int i = (int)popups.length() - 1; i >= 0; i--) {
        Ref<ElaraWidget> popup = getWidget(popups[i]);
        ElaraPopupWidget* popup_widget = asPopup(popup);

        if(!popup_widget || !popup_widget->isVisible()) {
            continue;
        }

        topmost_visible = popup_widget;

        if(popup_widget->contains(event.x, event.y)) {
            return popup_widget->eventPropagate(event);
        }
    }

    if(topmost_visible) {
        if(event.type == ELARA_UI_MOUSE_DOWN) {
            dismissAllPopups();
        }

        if(event.type == ELARA_UI_MOUSE_DOWN || event.type == ELARA_UI_MOUSE_UP) {
            return true;
        }
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        return c->eventPropagate(event);
    }

    return handleEvent(event);
}

void ElaraRootWidget::dispatchMouseMove(double px, double py) {
    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_MOVE;
    event.x = px;
    event.y = py;
    event.button = 0;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchMouseDown(int button, double px, double py) {
    Ref<ElaraWidget> focused_widget = getWidget(focus);
    ElaraTextInputWidget* focused_input = focused_widget
        ? dynamic_cast<ElaraTextInputWidget*>(focused_widget.getPtr())
        : 0;

    if(focused_input) {
        bool inside_focused =
            px >= focused_input->getAbsoluteX() &&
            py >= focused_input->getAbsoluteY() &&
            px <= focused_input->getAbsoluteX() + focused_input->getWidth() &&
            py <= focused_input->getAbsoluteY() + focused_input->getHeight();

        if(!inside_focused) {
            focused_input->setFocused(false);
            focus = ElaraWidgetHandle();
        }
    }

    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_DOWN;
    event.x = px;
    event.y = py;
    event.button = button;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchMouseUp(int button, double px, double py) {
    ElaraUiEvent event;
    event.root_widget = this;
    event.type = ELARA_UI_MOUSE_UP;
    event.x = px;
    event.y = py;
    event.button = button;

    eventPropagate(event);
}

void ElaraRootWidget::dispatchKeyDown(unsigned int keyval) {
    dispatchKeyDown(keyval, 0);
}

void ElaraRootWidget::dispatchKeyUp(unsigned int keyval) {
    dispatchKeyUp(keyval, 0);
}

void ElaraRootWidget::dispatchKeyDown(unsigned int keyval, unsigned int modifiers) {
    Ref<ElaraWidget> c = getWidget(content);

    if(c && c->dispatchAccelerator(keyval, modifiers)) {
        return;
    }

    Ref<ElaraWidget> focused_widget = getWidget(focus);

    if(focused_widget && focused_widget->isVisible()) {
        focused_widget->onKeyDown(keyval, modifiers);
        return;
    }

    if(c) {
        c->onKeyDown(keyval, modifiers);
    }
}

void ElaraRootWidget::dispatchKeyUp(unsigned int keyval, unsigned int modifiers) {
    Ref<ElaraWidget> focused_widget = getWidget(focus);

    if(focused_widget && focused_widget->isVisible()) {
        focused_widget->onKeyUp(keyval, modifiers);
        return;
    }

    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyUp(keyval, modifiers);
    }
}

void ElaraRootWidget::onMouseMove(double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseMove(px, py);
    }
}

void ElaraRootWidget::onMouseDown(int button, double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseDown(button, px, py);
    }
}

void ElaraRootWidget::onMouseUp(int button, double px, double py) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onMouseUp(button, px, py);
    }
}

void ElaraRootWidget::onKeyDown(unsigned int keyval) {
    onKeyDown(keyval, 0);
}

void ElaraRootWidget::onKeyUp(unsigned int keyval) {
    onKeyUp(keyval, 0);
}

void ElaraRootWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyDown(keyval, modifiers);
    }
}

void ElaraRootWidget::onKeyUp(unsigned int keyval, unsigned int modifiers) {
    Ref<ElaraWidget> c = getWidget(content);
    if(c) {
        c->onKeyUp(keyval, modifiers);
    }
}

}

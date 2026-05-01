#include "ElaraWidget.h"

namespace elara {

ElaraWidget::ElaraWidget() : 
    x(0),
    y(0),
    width(0),
    height(0),
    palette(0) { }

ElaraWidget::ElaraWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle)
    : widget_handle(widget_handle), 
      x(0),
      y(0),
      width(0),
      height(0),
      palette(0) {
        root_widget->registerWidget(widget_handle, this);
      }

ElaraWidget::~ElaraWidget() {}

void ElaraWidget::setPalette(ElaraPalette* widget_palette) {
    palette = widget_palette;
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

void ElaraWidget::onDraw(
    ElaraDrawContext* ctx,
    int draw_width,
    int draw_height
) {
    x = 0;
    y = 0;
    width = draw_width;
    height = draw_height;

    draw(ctx);
}

void ElaraWidget::addChild(Ref<ElaraWidget> child) {
    if(child) {
        child->setPalette(palette);
        children.push(child);
    }
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
    return px >= x && py >= y && px <= x + width && py <= y + height;
}

bool ElaraWidget::containsLocal(double px, double py) const {
    return px >= 0 && py >= 0 && px <= width && py <= height;
}

bool ElaraWidget::eventPropagate(ElaraUiEvent event) {
    bool is_mouse =
        event.type == ELARA_UI_MOUSE_MOVE ||
        event.type == ELARA_UI_MOUSE_DOWN ||
        event.type == ELARA_UI_MOUSE_UP;

    if(is_mouse) {
        int winner = -1;
        int winner_z = -2147483647;

        for(int i = 0; i < (int)children.length(); i++) {
            if(!children[i]) {
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

        if(winner >= 0) {
            ElaraUiEvent child_event = event;
            child_event.x = event.x - children[winner]->x;
            child_event.y = event.y - children[winner]->y;

            return children[winner]->eventPropagate(child_event);
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

        case ELARA_UI_KEY_DOWN:
            onKeyDown(event.keyval);
            return true;

        case ELARA_UI_KEY_UP:
            onKeyUp(event.keyval);
            return true;
    }

    return false;
}



}
#include "ElaraPopupWidget.h"

namespace elara {

ElaraPopupItem::ElaraPopupItem()
    : enabled(true) {}

ElaraPopupItem::ElaraPopupItem(const String& item_id, const String& item_text)
    : id(item_id),
      text(item_text),
      enabled(true) {}

String ElaraPopupItem::getId() const {
    return id;
}

String ElaraPopupItem::getText() const {
    return text;
}

bool ElaraPopupItem::isEnabled() const {
    return enabled;
}

void ElaraPopupItem::setEnabled(bool value) {
    enabled = value;
}

ElaraPopupWidget::ElaraPopupWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle)
    : ElaraWidget(root_widget, widget_handle), 
      visible(false),
      hover_index(-1),
      item_height(28),
      padding(8) {
    width = 180;
    height = 0;
}

void ElaraPopupWidget::showAt(double px, double py) {
    x = px;
    y = py;
    height = padding * 2 + items.length() * item_height;
    visible = true;
}

void ElaraPopupWidget::hide() {
    visible = false;
    hover_index = -1;
}

bool ElaraPopupWidget::isVisible() const {
    return visible;
}

void ElaraPopupWidget::clearItems() {
    items.clear();
    height = padding * 2;
}

void ElaraPopupWidget::addItem(const String& id, const String& text, bool enabled) {
    ElaraPopupItem item(id, text);
    item.setEnabled(enabled);
    items.push(item);
    height = padding * 2 + items.length() * item_height;
}

int ElaraPopupWidget::itemCount() const {
    return (int)items.length();
}

int ElaraPopupWidget::itemAt(double px, double py) const {
    if(!visible) {
        return -1;
    }

    if(px < x || py < y || px > x + width || py > y + height) {
        return -1;
    }

    double local_y = py - y - padding;

    if(local_y < 0) {
        return -1;
    }

    int index = (int)(local_y / item_height);

    if(index < 0 || index >= (int)items.length()) {
        return -1;
    }

    return index;
}

void ElaraPopupWidget::draw(ElaraDrawContext* ctx) {
    if(!visible) {
        return;
    }

    ElaraPaletteTriplet popup_colors = colors("popup", "default");
    ElaraPaletteTriplet hover_colors = colors("popup", "hover");

    ElaraColor bg = popup_colors.base;
    ctx->setColor(bg.r, bg.g, bg.b);
    ctx->fillRect(x, y, width, height);

    ElaraColor border = popup_colors.accent;
    ctx->setColor(border.r, border.g, border.b);
    ctx->line(x, y, x + width, y, 1);
    ctx->line(x, y + height, x + width, y + height, 1);
    ctx->line(x, y, x, y + height, 1);
    ctx->line(x + width, y, x + width, y + height, 1);

    for(int i = 0; i < (int)items.length(); i++) {
        double iy = y + padding + i * item_height;

        if(i == hover_index) {
            ElaraColor h = hover_colors.base;
            ctx->setColor(h.r, h.g, h.b);
            ctx->fillRect(x + 4, iy, width - 8, item_height);
        }

        ElaraColor text_color = items[i].isEnabled()
            ? popup_colors.text
            : popup_colors.accent;

        ctx->setColor(text_color.r, text_color.g, text_color.b);
        ctx->drawText(x + 12, iy + 19, items[i].getText(), 13);
    }
}

void ElaraPopupWidget::onMouseMove(double px, double py) {
    hover_index = itemAt(px, py);
}

void ElaraPopupWidget::onMouseDown(int button, double px, double py) {
    int index = itemAt(px, py);

    if(index < 0) {
        hide();
        return;
    }

    if(items[index].isEnabled()) {
        onItemSelected(items[index].getId());
    }

    hide();
}

void ElaraPopupWidget::onMouseUp(int button, double px, double py) {
}

}

#include "ElaraToolBarWidget.h"

namespace elara {

ElaraToolBarWidget::ElaraToolBarWidget(
    ElaraWidgetRegister* widget_register,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(widget_register, widget_handle),
    orientation(ELARA_TOOLBAR_HORIZONTAL),
    palette_master("button"),
    font_size(14),
    item_padding_x(10),
    item_padding_y(6),
    item_spacing(2),
    separator_size(7),
    icon_text_gap(6),
    hover_index(-1),
    pressed_index(-1),
    layout_valid(false) {
}

ElaraToolBarWidget::~ElaraToolBarWidget() {}

void ElaraToolBarWidget::invalidateLayout() {
    layout_metrics.clear();
    layout_valid = false;
}

String ElaraToolBarWidget::displayTextForItem(int index) const {
    if(index < 0 || index >= (int)items.length()) {
        return String();
    }

    if(items[index].icon.length() > 0 && items[index].text.length() > 0) {
        return items[index].icon + String(" ") + items[index].text;
    }

    if(items[index].icon.length() > 0) {
        return items[index].icon;
    }

    return items[index].text;
}

bool ElaraToolBarWidget::itemIsActionable(int index) const {
    return index >= 0 &&
           index < (int)items.length() &&
           !items[index].separator &&
           items[index].enabled;
}

double ElaraToolBarWidget::estimatedTextWidth(const String& value) const {
    return value.length() * font_size * 0.62;
}

void ElaraToolBarWidget::itemPreferredSize(
    int index,
    ElaraDrawContext* ctx,
    double* out_w,
    double* out_h
) const {
    double w = 0;
    double h = 0;

    if(index < 0 || index >= (int)items.length()) {
        if(out_w) { *out_w = 0; }
        if(out_h) { *out_h = 0; }
        return;
    }

    if(items[index].separator) {
        if(orientation == ELARA_TOOLBAR_HORIZONTAL) {
            w = separator_size;
            h = height;
        } else {
            w = width;
            h = separator_size;
        }
    } else {
        String label = displayTextForItem(index);
        double text_width = ctx
            ? ctx->measureTextWidth(label, font_size)
            : estimatedTextWidth(label);

        w = text_width + item_padding_x * 2;
        h = font_size + item_padding_y * 2;

        if(w < h) {
            w = h;
        }
    }

    if(out_w) { *out_w = w; }
    if(out_h) { *out_h = h; }
}

void ElaraToolBarWidget::rebuildLayout(ElaraDrawContext* ctx) {
    invalidateLayout();

    double cursor = 0;

    for(int i = 0; i < (int)items.length(); i++) {
        double item_w = 0;
        double item_h = 0;
        itemPreferredSize(i, ctx, &item_w, &item_h);

        ToolBarLayoutMetric metric;

        if(orientation == ELARA_TOOLBAR_HORIZONTAL) {
            metric.x = cursor;
            metric.y = 0;
            metric.width = item_w;
            metric.height = height;
            cursor += item_w + item_spacing;
        } else {
            metric.x = 0;
            metric.y = cursor;
            metric.width = width;
            metric.height = item_h;
            cursor += item_h + item_spacing;
        }

        layout_metrics.push(metric);
    }

    layout_valid = true;
}

int ElaraToolBarWidget::itemAt(double px, double py, ElaraDrawContext* ctx) const {
    if(px < 0 || py < 0 || px > width || py > height) {
        return -1;
    }

    ElaraToolBarWidget* self = const_cast<ElaraToolBarWidget*>(this);
    if(ctx && !layout_valid) {
        self->rebuildLayout(ctx);
    }

    if(!layout_valid) {
        self->rebuildLayout(0);
    }

    for(int i = 0; i < (int)layout_metrics.length(); i++) {
        ToolBarLayoutMetric metric = layout_metrics[i];
        if(px >= metric.x &&
           py >= metric.y &&
           px <= metric.x + metric.width &&
           py <= metric.y + metric.height) {
            return i;
        }
    }

    return -1;
}

void ElaraToolBarWidget::clearItems() {
    items.clear();
    invalidateLayout();
    hover_index = -1;
    pressed_index = -1;
}

void ElaraToolBarWidget::addItem(
    const String& item_id,
    const String& text,
    const String& icon,
    bool enabled,
    const String& tooltip
) {
    if(item_id.length() <= 0) {
        return;
    }

    ToolBarItem item;
    item.id = item_id;
    item.text = text;
    item.icon = icon;
    item.tooltip = tooltip;
    item.enabled = enabled;
    item.separator = false;
    items.push(item);
    invalidateLayout();
}

void ElaraToolBarWidget::addSeparator() {
    ToolBarItem item;
    item.id = String();
    item.text = String();
    item.icon = String();
    item.tooltip = String();
    item.enabled = false;
    item.separator = true;
    items.push(item);
    invalidateLayout();
}

int ElaraToolBarWidget::itemCount() const {
    return (int)items.length();
}

String ElaraToolBarWidget::getItemId(int index) const {
    if(index < 0 || index >= (int)items.length()) {
        return String();
    }

    return items[index].id;
}

void ElaraToolBarWidget::setOrientation(ElaraToolBarOrientation value) {
    if(orientation == value) {
        return;
    }

    orientation = value;
    invalidateLayout();
    hover_index = -1;
    pressed_index = -1;
}

ElaraToolBarOrientation ElaraToolBarWidget::getOrientation() const {
    return orientation;
}

void ElaraToolBarWidget::setHorizontal(bool horizontal) {
    setOrientation(horizontal ? ELARA_TOOLBAR_HORIZONTAL : ELARA_TOOLBAR_VERTICAL);
}

bool ElaraToolBarWidget::isHorizontal() const {
    return orientation == ELARA_TOOLBAR_HORIZONTAL;
}

void ElaraToolBarWidget::setFontSize(double size) {
    if(size <= 0) {
        return;
    }

    font_size = size;
    invalidateLayout();
}

double ElaraToolBarWidget::getFontSize() const {
    return font_size;
}

void ElaraToolBarWidget::setItemPadding(double x, double y) {
    item_padding_x = x < 0 ? 0 : x;
    item_padding_y = y < 0 ? 0 : y;
    invalidateLayout();
}

void ElaraToolBarWidget::setItemSpacing(double value) {
    item_spacing = value < 0 ? 0 : value;
    invalidateLayout();
}

void ElaraToolBarWidget::setItemEnabled(const String& item_id, bool enabled) {
    for(int i = 0; i < (int)items.length(); i++) {
        if(!items[i].separator && items[i].id == item_id) {
            items[i].enabled = enabled;
            if(!enabled && (hover_index == i || pressed_index == i)) {
                hover_index = -1;
                pressed_index = -1;
            }
            return;
        }
    }
}

bool ElaraToolBarWidget::isItemEnabled(const String& item_id) const {
    for(int i = 0; i < (int)items.length(); i++) {
        if(!items[i].separator && items[i].id == item_id) {
            return items[i].enabled;
        }
    }

    return false;
}

ElaraMouseCursor ElaraToolBarWidget::cursor() const {
    return hover_index >= 0 ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

ElaraMouseCursor ElaraToolBarWidget::cursorAt(double px, double py) const {
    int index = itemAt(px, py);
    return itemIsActionable(index) ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraToolBarWidget::draw(ElaraDrawContext* ctx) {
    rebuildLayout(ctx);

    ElaraPaletteTriplet default_colors = colors(palette_master, "default");
    ElaraPaletteTriplet hover_colors = colors(palette_master, "hover");
    ElaraPaletteTriplet active_colors = colors(palette_master, "active");
    ElaraPaletteTriplet disabled_colors = colors(palette_master, "disabled");

    ctx->setColor(default_colors.base.r, default_colors.base.g, default_colors.base.b);
    ctx->fillRect(0, 0, width, height);

    for(int i = 0; i < (int)items.length(); i++) {
        ToolBarLayoutMetric metric = layout_metrics[i];

        if(items[i].separator) {
            ctx->setColor(default_colors.accent.r, default_colors.accent.g, default_colors.accent.b);

            if(orientation == ELARA_TOOLBAR_HORIZONTAL) {
                double sx = metric.x + metric.width / 2.0;
                ctx->line(sx, item_padding_y, sx, height - item_padding_y, 1);
            } else {
                double sy = metric.y + metric.height / 2.0;
                ctx->line(item_padding_x, sy, width - item_padding_x, sy, 1);
            }

            continue;
        }

        ElaraPaletteTriplet item_colors = default_colors;
        bool hovered_item = i == hover_index;
        bool pressed_item = i == pressed_index;

        if(!items[i].enabled) {
            item_colors = disabled_colors;
        } else if(pressed_item) {
            item_colors = active_colors;
        } else if(hovered_item) {
            item_colors = hover_colors;
        }

        if(hovered_item || pressed_item || !items[i].enabled) {
            ctx->setColor(item_colors.base.r, item_colors.base.g, item_colors.base.b);
            ctx->fillRect(metric.x, metric.y, metric.width, metric.height);
        }

        String label = displayTextForItem(i);
        double text_w = ctx->measureTextWidth(label, font_size);
        double text_x = metric.x + item_padding_x;
        double text_y = metric.y + (metric.height / 2.0) + (font_size / 2.0) - 2;

        if(orientation == ELARA_TOOLBAR_VERTICAL && text_w < metric.width - item_padding_x * 2) {
            text_x = metric.x + (metric.width - text_w) / 2.0;
        }

        ctx->setColor(item_colors.text.r, item_colors.text.g, item_colors.text.b);
        ctx->drawText(text_x, text_y, label, font_size);
    }
}

void ElaraToolBarWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);

    int previous_hover = hover_index;
    int index = containsLocal(px, py) ? itemAt(px, py) : -1;
    hover_index = itemIsActionable(index) ? index : -1;

    if(previous_hover != hover_index) {
        emitHoverChanged(hover_index >= 0);
    }
}

void ElaraToolBarWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if(button != 1) {
        return;
    }

    int index = containsLocal(px, py) ? itemAt(px, py) : -1;
    pressed_index = itemIsActionable(index) ? index : -1;
}

void ElaraToolBarWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    int release_index = containsLocal(px, py) ? itemAt(px, py) : -1;
    int clicked_index = pressed_index;
    pressed_index = -1;

    if(button == 1 && clicked_index >= 0 && clicked_index == release_index && itemIsActionable(clicked_index)) {
        emitClicked(button, px, py);
        emitAction(items[clicked_index].id);
    }
}

}

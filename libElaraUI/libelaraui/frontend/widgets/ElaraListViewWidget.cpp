#include "ElaraListViewWidget.h"

namespace elara {

ElaraListViewItem::ElaraListViewItem() {
}

ElaraListViewItem::ElaraListViewItem(const String& item_id, const String& item_text)
    : id(item_id),
      text(item_text) {
}

void ElaraListViewItem::setId(const String& value) {
    id = value;
}

String ElaraListViewItem::getId() const {
    return id;
}

void ElaraListViewItem::setText(const String& value) {
    text = value;
}

String ElaraListViewItem::getText() const {
    return text;
}

ElaraListViewWidget::ElaraListViewWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    items(),
    palette_master("panel"),
    selected_id(""),
    selected_text(""),
    enabled(true),
    hover_index(-1),
    font_size(14),
    row_height(24),
    padding_x(8) {
}

ElaraListViewWidget::~ElaraListViewWidget() {
}

int ElaraListViewWidget::rowAt(double py) const {
    if(py < 0 || py > height) {
        return -1;
    }

    int index = (int)(py / row_height);

    if(index < 0 || index >= (int)items.length()) {
        return -1;
    }

    return index;
}

void ElaraListViewWidget::clearItems() {
    items.clear();
    selected_id = "";
    selected_text = "";
    hover_index = -1;
}

void ElaraListViewWidget::addItem(const ElaraListViewItem& item) {
    items.push(item);
}

void ElaraListViewWidget::setEnabled(bool value) {
    enabled = value;

    if(!enabled) {
        hover_index = -1;
    }
}

bool ElaraListViewWidget::isEnabled() const {
    return enabled;
}

void ElaraListViewWidget::setFontSize(double value) {
    font_size = value;
    row_height = font_size + 10;
}

double ElaraListViewWidget::getFontSize() const {
    return font_size;
}

String ElaraListViewWidget::getSelectedId() const {
    return selected_id;
}

String ElaraListViewWidget::getSelectedText() const {
    return selected_text;
}

int ElaraListViewWidget::getItemCount() const {
    return (int)items.length();
}

ElaraMouseCursor ElaraListViewWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraListViewWidget::draw(ElaraDrawContext* ctx) {
    String sub = enabled ? String("default") : String("disabled");
    ElaraPaletteTriplet c = colors(palette_master, sub);
    ElaraPaletteTriplet hover = colors("button", "hover");
    ElaraPaletteTriplet selected = colors("button", "pressed");

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    for(int i = 0; i < (int)items.length(); i++) {
        double y = i * row_height;

        if(y + row_height > height) {
            break;
        }

        if(items[i].getId() == selected_id) {
            ctx->setColor(selected.base.r, selected.base.g, selected.base.b);
            ctx->fillRect(2, y + 1, width - 4, row_height - 2);
        } else if(i == hover_index) {
            ctx->setColor(hover.base.r, hover.base.g, hover.base.b);
            ctx->fillRect(2, y + 1, width - 4, row_height - 2);
        }

        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->drawText(padding_x, y + font_size + 5, items[i].getText(), font_size);
    }
}

void ElaraListViewWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);
    hover_index = rowAt(py);
}

void ElaraListViewWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);
}

void ElaraListViewWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);

    if(!enabled || button != 1) {
        return;
    }

    int row_index = rowAt(py);

    if(row_index < 0 || row_index >= (int)items.length()) {
        return;
    }

    selected_id = items[row_index].getId();
    selected_text = items[row_index].getText();
    emitValueChanged(1.0);
    emitClicked(button, px, py);
}

}

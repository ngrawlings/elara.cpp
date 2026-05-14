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
    padding_x(8),
    scroll_offset(0),
    scrollbar_size(14),
    vscroll(new ElaraSliderWidget(
        root_widget,
        ElaraWidgetHandle(
            String((const char*)widget_handle.getHandle().getPtr(), widget_handle.getHandle().length()) +
            String(".vscroll")
        )
    )) {
    vscroll->setOrientation("vertical");
    vscroll->setStep(1);
    vscroll->setZOrder(10);
    addChild(Ref<ElaraWidget>(vscroll));
}

ElaraListViewWidget::~ElaraListViewWidget() {
}

int ElaraListViewWidget::rowAt(double py) const {
    if(py < 0 || py > height) {
        return -1;
    }

    int index = (int)(py / row_height) + scroll_offset;

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
    scroll_offset = 0;
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

bool ElaraListViewWidget::acceptsDoubleClick() const {
    return true;
}

ElaraMouseCursor ElaraListViewWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraListViewWidget::draw(ElaraDrawContext* ctx) {
    int visible_rows = height > 0 ? (int)(height / row_height) : 0;
    int max_scroll = (int)items.length() - visible_rows;
    if(max_scroll < 0) max_scroll = 0;

    if(scroll_offset > max_scroll) scroll_offset = max_scroll;
    if(scroll_offset < 0) scroll_offset = 0;

    bool show_scroll = max_scroll > 0;
    vscroll->setVisible(show_scroll);

    if(show_scroll) {
        vscroll->setRange(0, max_scroll);
        vscroll->setStep(1);
        vscroll->setValue(scroll_offset);
        vscroll->setBounds(width - scrollbar_size, 0, scrollbar_size, height);
    }

    double content_width = show_scroll ? width - scrollbar_size : width;

    String sub = enabled ? String("default") : String("disabled");
    ElaraPaletteTriplet c = colors(palette_master, sub);
    ElaraPaletteTriplet hover_c = colors("button", "hover");
    ElaraPaletteTriplet selected_c = colors("button", "pressed");

    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, content_width, height);

    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, content_width, 0, 1);
    ctx->line(0, height - 1, content_width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(content_width - 1, 0, content_width - 1, height, 1);

    for(int i = scroll_offset; i < (int)items.length(); i++) {
        double row_y = (i - scroll_offset) * row_height;

        if(row_y + row_height > height) {
            break;
        }

        if(items[i].getId() == selected_id) {
            ctx->setColor(selected_c.base.r, selected_c.base.g, selected_c.base.b);
            ctx->fillRect(2, row_y + 1, content_width - 4, row_height - 2);
        } else if(i == hover_index) {
            ctx->setColor(hover_c.base.r, hover_c.base.g, hover_c.base.b);
            ctx->fillRect(2, row_y + 1, content_width - 4, row_height - 2);
        }

        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->drawText(padding_x, row_y + font_size + 5, items[i].getText(), font_size);
    }

    if(show_scroll) {
        vscroll->onDraw(ctx, (int)scrollbar_size, (int)height);
    }
}

bool ElaraListViewWidget::eventPropagate(ElaraUiEvent event) {
    if(vscroll->isVisible()) {
        vscroll->setBounds(width - scrollbar_size, 0, scrollbar_size, height);
    }

    if(event.type == ELARA_UI_MOUSE_SCROLL) {
        onMouseScroll(event.scroll_dx, event.scroll_dy);
        return true;
    }

    bool handled = ElaraWidget::eventPropagate(event);

    if(vscroll->isVisible()) {
        scroll_offset = (int)vscroll->getValue();
    }

    return handled;
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

    selected_id   = items[row_index].getId();
    selected_text = items[row_index].getText();
    emitValueChanged(1.0);
    emitClicked(button, px, py);
}

void ElaraListViewWidget::onMouseDoubleClick(int button, double px, double py) {
    if(!enabled || button != 1) {
        return;
    }

    int row_index = rowAt(py);

    if(row_index < 0 || row_index >= (int)items.length()) {
        return;
    }

    emitAction(items[row_index].getId());
}

void ElaraListViewWidget::onMouseScroll(double dx, double dy) {
    (void)dx;
    int visible_rows = (int)(height / row_height);
    int max_scroll = (int)items.length() - visible_rows;
    if(max_scroll <= 0) {
        return;
    }
    scroll_offset += (int)(dy + (dy > 0 ? 0.5 : -0.5));
    if(scroll_offset < 0) scroll_offset = 0;
    if(scroll_offset > max_scroll) scroll_offset = max_scroll;
    if(vscroll->isVisible()) {
        vscroll->setValue(scroll_offset);
    }
}

}

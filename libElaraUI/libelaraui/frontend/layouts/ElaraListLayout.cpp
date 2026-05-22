#include "ElaraListLayout.h"

namespace elara {

ElaraListEntry::ElaraListEntry()
    : row_height(32) {}

ElaraListEntry::ElaraListEntry(ElaraWidgetHandle handle, double height)
    : widget_handle(handle),
      row_height(height > 0 ? height : 32) {}

ElaraListLayout::ElaraListLayout(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
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
    vscroll_ref = Ref<ElaraWidget>(vscroll);
    addChild(vscroll_ref);
}

ElaraListLayout::~ElaraListLayout() {}

double ElaraListLayout::totalContentHeight() const {
    double total = 0;
    for(int i = 0; i < (int)entries.length(); i++) {
        total += entries[i].row_height;
    }
    return total;
}

void ElaraListLayout::addEntry(ElaraWidgetHandle widget_handle, double row_height) {
    entries.push(ElaraListEntry(widget_handle, row_height));
}

void ElaraListLayout::clearEntries() {
    entries.clear();
    scroll_offset = 0;
}

void ElaraListLayout::clearEntryChildren() {
    entries.clear();
    scroll_offset = 0;
    /* vscroll_ref keeps vscroll alive regardless of what clearChildren does */
    clearChildren();
    addChild(vscroll_ref);
}

double ElaraListLayout::getScrollOffset() const {
    return scroll_offset;
}

void ElaraListLayout::setScrollOffset(double value) {
    scroll_offset = value;
    if(scroll_offset < 0) scroll_offset = 0;
}

/* Recompute and store each entry child's x/y/w/h so that eventPropagate
   (which uses the stored bounds for hit-testing) is always current. */
void ElaraListLayout::updateBounds() {
    double total = totalContentHeight();
    double max_scroll = total - height;
    if(max_scroll < 0) max_scroll = 0;
    if(scroll_offset > max_scroll) scroll_offset = max_scroll;
    if(scroll_offset < 0) scroll_offset = 0;

    bool show_scroll = max_scroll > 0;
    double content_width = show_scroll ? width - scrollbar_size : width;

    if(show_scroll) {
        vscroll->setVisible(true);
        vscroll->setRange(0, max_scroll);
        vscroll->setStep(entries.length() > 0 ? entries[0].row_height : 1);
        vscroll->setValue(scroll_offset);
        vscroll->setBounds(content_width, 0, scrollbar_size, height);
    } else {
        vscroll->setVisible(false);
    }

    /* Position each entry child in viewport space. */
    double y_cursor = -scroll_offset;
    int child_offset = 1; /* child 0 is vscroll */

    for(int i = 0; i < (int)entries.length(); i++) {
        double entry_h = entries[i].row_height;
        Ref<ElaraWidget> widget = getChild(child_offset + i);
        if(widget) {
            widget->setParent(this);
            widget->setBounds(0, y_cursor, content_width, entry_h);
        }
        y_cursor += entry_h;
    }
}

void ElaraListLayout::draw(ElaraDrawContext* ctx) {
    double total = totalContentHeight();
    double max_scroll = total - height;
    if(max_scroll < 0) max_scroll = 0;
    if(scroll_offset > max_scroll) scroll_offset = max_scroll;
    if(scroll_offset < 0) scroll_offset = 0;

    bool show_scroll = max_scroll > 0;
    double content_width = show_scroll ? width - scrollbar_size : width;

    ElaraPaletteTriplet c = colors("panel", "default");
    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    if(show_scroll) {
        vscroll->setVisible(true);
        vscroll->setRange(0, max_scroll);
        vscroll->setStep(entries.length() > 0 ? entries[0].row_height : 1);
        vscroll->setValue(scroll_offset);
        vscroll->setBounds(content_width, 0, scrollbar_size, height);
    } else {
        vscroll->setVisible(false);
    }

    int child_offset = 1; /* child 0 is vscroll */
    double y_cursor = -scroll_offset;

    for(int i = 0; i < (int)entries.length(); i++) {
        double entry_h = entries[i].row_height;

        /* Skip entries entirely above or below the viewport. */
        if(y_cursor + entry_h <= 0 || y_cursor >= height) {
            y_cursor += entry_h;
            continue;
        }

        Ref<ElaraWidget> widget = getChild(child_offset + i);
        if(!widget || !widget->isVisible()) {
            y_cursor += entry_h;
            continue;
        }

        widget->setParent(this);
        widget->setBounds(0, y_cursor, content_width, entry_h);
        widget->onDraw(ctx, (int)content_width, (int)entry_h);

        y_cursor += entry_h;
    }

    if(show_scroll) {
        vscroll->onDraw(ctx, (int)scrollbar_size, (int)height);
    }
}

bool ElaraListLayout::eventPropagate(ElaraUiEvent event) {
    if(!visible) {
        return false;
    }

    /* Sync bounds before hit-testing so positions are current. */
    updateBounds();

    if(event.type == ELARA_UI_MOUSE_SCROLL) {
        onMouseScroll(event.scroll_dx, event.scroll_dy);
        return true;
    }

    bool handled = ElaraWidget::eventPropagate(event);

    if(vscroll->isVisible()) {
        scroll_offset = vscroll->getValue();
    }

    return handled;
}

void ElaraListLayout::onMouseScroll(double dx, double dy) {
    (void)dx;
    double total = totalContentHeight();
    double max_scroll = total - height;
    if(max_scroll <= 0) {
        return;
    }

    double step = entries.length() > 0 ? entries[0].row_height : 32;
    scroll_offset += dy * step;

    if(scroll_offset < 0) scroll_offset = 0;
    if(scroll_offset > max_scroll) scroll_offset = max_scroll;

    if(vscroll->isVisible()) {
        vscroll->setValue(scroll_offset);
    }
}

}

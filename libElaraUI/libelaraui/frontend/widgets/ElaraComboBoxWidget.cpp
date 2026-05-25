#include "ElaraComboBoxWidget.h"
#include "ElaraPopupWidget.h"
#include "ElaraRootWidget.h"

namespace elara {

static String truncateText(ElaraDrawContext* ctx, const String& text, double max_width, double font_size) {
    if(ctx->measureTextWidth(text, font_size) <= max_width) {
        return text;
    }
    String ellipsis("\xe2\x80\xa6");  // UTF-8 for U+2026 HORIZONTAL ELLIPSIS
    double ellipsis_w = ctx->measureTextWidth(ellipsis, font_size);
    if(ellipsis_w >= max_width) {
        return ellipsis;
    }
    double avail = max_width - ellipsis_w;
    int len = (int)text.length();
    while(len > 0 && ctx->measureTextWidth(text.substr(0, len), font_size) > avail) {
        len--;
    }
    return text.substr(0, len) + ellipsis;
}

// ── Dropdown popup ────────────────────────────────────────────────────────────

class ElaraComboDropdownWidget : public ElaraPopupWidget {
private:
    ElaraComboBoxWidget* combo;
    double item_height;
    double padding;
    int hover_index;
    double font_size;
    Array<ElaraComboItem>* source_items;

    int itemAt(double px, double py) const {
        if(!isVisible()) {
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

        if(!source_items || index < 0 || index >= (int)source_items->length()) {
            return -1;
        }

        return index;
    }

public:
    ElaraComboDropdownWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle,
        ElaraComboBoxWidget* owner,
        Array<ElaraComboItem>* items
    ) : ElaraPopupWidget(root_widget, widget_handle),
        combo(owner),
        item_height(26),
        padding(4),
        hover_index(-1),
        font_size(13),
        source_items(items) {}

    void syncFromCombo(double combo_width) {
        width = combo_width;
        height = padding * 2 + (source_items ? source_items->length() * item_height : 0);
        hover_index = -1;
    }

    void draw(ElaraDrawContext* ctx) {
        if(!isVisible() || !source_items) {
            return;
        }

        ElaraPaletteTriplet c = colors("panel", "default");
        ElaraPaletteTriplet hover_c = colors("button", "hover");
        ElaraPaletteTriplet selected_c = colors("button", "pressed");

        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(x, y, width, height);

        ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
        ctx->line(x, y, x + width, y, 1);
        ctx->line(x, y + height, x + width, y + height, 1);
        ctx->line(x, y, x, y + height, 1);
        ctx->line(x + width - 1, y, x + width - 1, y + height, 1);

        String sel_id = combo ? combo->getSelectedId() : String();

        for(int i = 0; i < (int)source_items->length(); i++) {
            double iy = y + padding + i * item_height;

            if((*source_items)[i].getId() == sel_id) {
                ctx->setColor(selected_c.base.r, selected_c.base.g, selected_c.base.b);
                ctx->fillRect(x + 2, iy, width - 4, item_height);
            } else if(i == hover_index) {
                ctx->setColor(hover_c.base.r, hover_c.base.g, hover_c.base.b);
                ctx->fillRect(x + 2, iy, width - 4, item_height);
            }

            ctx->setColor(c.text.r, c.text.g, c.text.b);
            double item_max_w = width - 20;  // 10px left pad + 10px right margin
            ctx->drawText(x + 10, iy + font_size + 4, truncateText(ctx, (*source_items)[i].getText(), item_max_w, font_size), font_size);
        }
    }

    void onMouseMove(double px, double py) {
        hover_index = itemAt(px, py);
    }

    void onMouseDown(int button, double px, double py) {
        (void)button;
        (void)px;
        (void)py;
    }

    ElaraRootWidget* getRoot() const {
        ElaraWidget* cursor = parent;
        while(cursor && cursor->getParent()) {
            cursor = cursor->getParent();
        }
        return cursor ? dynamic_cast<ElaraRootWidget*>(cursor) : 0;
    }

    void onMouseUp(int button, double px, double py) {
        if(button != 1 || !source_items) {
            return;
        }

        int index = itemAt(px, py);

        ElaraRootWidget* root = getRoot();
        if(root) {
            root->dismissAllPopups();
        } else {
            hide();
        }

        if(index < 0 || index >= (int)source_items->length()) {
            return;
        }

        String id = (*source_items)[index].getId();

        if(combo) {
            combo->onDropdownItemSelected(id);
        }
    }

    void onItemSelected(const String& id) {
        (void)id;
    }
};

// ── ElaraComboItem ────────────────────────────────────────────────────────────

ElaraComboItem::ElaraComboItem() {}

ElaraComboItem::ElaraComboItem(const String& item_id, const String& item_text)
    : id(item_id), text(item_text) {}

String ElaraComboItem::getId() const {
    return id;
}

String ElaraComboItem::getText() const {
    return text;
}

// ── ElaraComboBoxWidget ───────────────────────────────────────────────────────

ElaraComboBoxWidget::ElaraComboBoxWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    selected_id(""),
    selected_text(""),
    enabled(true),
    hovered(false),
    pressed(false),
    font_size(13),
    arrow_width(26) {

    String id_str((const char*)widget_handle.getHandle().getPtr(), widget_handle.getHandle().length());
    dropdown_handle = ElaraWidgetHandle(id_str + String(".dropdown"));

    new ElaraComboDropdownWidget(root_widget, dropdown_handle, this, &items);
}

ElaraComboBoxWidget::~ElaraComboBoxWidget() {}

void ElaraComboBoxWidget::clearItems() {
    items.clear();
    selected_id = "";
    selected_text = "";
}

void ElaraComboBoxWidget::addItem(const String& id, const String& item_text) {
    items.push(ElaraComboItem(id, item_text));
    if(selected_id.length() == 0 && items.length() == 1) {
        selected_id = id;
        selected_text = item_text;
    }
}

int ElaraComboBoxWidget::getItemCount() const {
    return (int)items.length();
}

void ElaraComboBoxWidget::setSelectedId(const String& id) {
    for(int i = 0; i < (int)items.length(); i++) {
        if(items[i].getId() == id) {
            selected_id = id;
            selected_text = items[i].getText();
            return;
        }
    }
}

void ElaraComboBoxWidget::setSelectedText(const String& text_val) {
    for(int i = 0; i < (int)items.length(); i++) {
        if(items[i].getText() == text_val) {
            selected_id = items[i].getId();
            selected_text = text_val;
            return;
        }
    }
}

String ElaraComboBoxWidget::getSelectedId() const {
    return selected_id;
}

String ElaraComboBoxWidget::getSelectedText() const {
    return selected_text;
}

void ElaraComboBoxWidget::setEnabled(bool value) {
    enabled = value;
    if(!enabled) {
        hovered = false;
        pressed = false;
    }
}

bool ElaraComboBoxWidget::isEnabled() const {
    return enabled;
}

void ElaraComboBoxWidget::setFontSize(double value) {
    font_size = value;
}

ElaraRootWidget* ElaraComboBoxWidget::rootWidget() const {
    ElaraWidget* cursor = parent;
    while(cursor && cursor->getParent()) {
        cursor = cursor->getParent();
    }
    return cursor ? dynamic_cast<ElaraRootWidget*>(cursor) : 0;
}

void ElaraComboBoxWidget::openDropdown() {
    ElaraRootWidget* root = rootWidget();
    if(!root) {
        return;
    }

    Ref<ElaraWidget> dropdown_widget = root->getWidget(dropdown_handle);
    ElaraComboDropdownWidget* dropdown = dynamic_cast<ElaraComboDropdownWidget*>(dropdown_widget.getPtr());
    if(!dropdown) {
        return;
    }

    dropdown->setPalette(getPalette());
    root->pushPopup(dropdown_handle);
    dropdown->showAt(getAbsoluteX(), getAbsoluteY() + height);
    dropdown->syncFromCombo(width);
}

void ElaraComboBoxWidget::onDropdownItemSelected(const String& id) {
    for(int i = 0; i < (int)items.length(); i++) {
        if(items[i].getId() == id) {
            selected_id = id;
            selected_text = items[i].getText();
            emitValueChanged(1.0);
            emitAction(id);
            return;
        }
    }
}

void ElaraComboBoxWidget::draw(ElaraDrawContext* ctx) {
    ElaraRootWidget* root = rootWidget();
    bool is_open = false;
    if(root) {
        Ref<ElaraWidget> dw = root->getWidget(dropdown_handle);
        ElaraPopupWidget* dp = dynamic_cast<ElaraPopupWidget*>(dw.getPtr());
        is_open = dp && dp->isVisible();
    }

    String sub("default");
    if(!enabled) {
        sub = String("disabled");
    } else if(is_open || pressed) {
        sub = String("pressed");
    } else if(hovered) {
        sub = String("hover");
    }

    ElaraPaletteTriplet c = colors("button", sub);

    // Background
    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(0, 0, width, height);

    // Border
    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(0, 0, width, 0, 1);
    ctx->line(0, height - 1, width, height - 1, 1);
    ctx->line(0, 0, 0, height, 1);
    ctx->line(width - 1, 0, width - 1, height, 1);

    // Arrow divider
    ctx->line(width - arrow_width, 2, width - arrow_width, height - 2, 1);

    // Arrow glyph (▾)
    double arrow_cx = width - arrow_width / 2;
    double arrow_cy = height / 2;
    ctx->line(arrow_cx - 4, arrow_cy - 2, arrow_cx, arrow_cy + 3, 1.5);
    ctx->line(arrow_cx, arrow_cy + 3, arrow_cx + 4, arrow_cy - 2, 1.5);

    // Selected text — clipped to avoid overflowing into the arrow area
    ctx->setColor(c.text.r, c.text.g, c.text.b);
    double text_y = (height / 2) + (font_size / 2) - 2;
    double text_max_w = width - arrow_width - 8 - 4;  // left pad, arrow zone, right margin
    ctx->drawText(8, text_y, truncateText(ctx, selected_text, text_max_w, font_size), font_size);
}

ElaraMouseCursor ElaraComboBoxWidget::cursor() const {
    return enabled ? ELARA_CURSOR_POINTER : ELARA_CURSOR_DEFAULT;
}

void ElaraComboBoxWidget::onMouseMove(double px, double py) {
    bool was_hovered = hovered;
    hovered = containsLocal(px, py);
    emitMouseMove(px, py);
    if(was_hovered != hovered) {
        emitHoverChanged(hovered);
    }
    if(!hovered) {
        pressed = false;
    }
}

void ElaraComboBoxWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);
    if(!enabled || button != 1) {
        return;
    }
    if(containsLocal(px, py)) {
        pressed = true;
    }
}

void ElaraComboBoxWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
    if(!enabled || button != 1) {
        pressed = false;
        return;
    }
    bool was_pressed = pressed;
    pressed = false;
    if(was_pressed && containsLocal(px, py)) {
        openDropdown();
    }
}

bool ElaraComboBoxWidget::wantsFocus() const {
    return enabled;
}

}

#include "ElaraMenuBarWidget.h"

#include "ElaraRootWidget.h"

namespace elara {

namespace {

String handleString(const ElaraWidgetHandle& handle) {
    Memory memory = handle.getHandle();
    return String((const char*)memory.getPtr(), memory.length());
}

}

class ElaraMenuBarWidget::MenuPopupWidget : public ElaraPopupWidget {
private:
    ElaraMenuBarWidget* owner;
    String menu_id;

public:
    MenuPopupWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle,
        ElaraMenuBarWidget* owner_widget,
        const String& owner_menu_id
    ) : ElaraPopupWidget(root_widget, widget_handle),
        owner(owner_widget),
        menu_id(owner_menu_id) {
    }

    void onItemSelected(const String& id) {
        if(owner) {
            owner->onMenuAction(menu_id, id);
        }
    }
};

namespace {

ElaraMenuBarWidget::MenuPopupWidget* asMenuPopup(Ref<ElaraWidget> widget) {
    return widget ? dynamic_cast<ElaraMenuBarWidget::MenuPopupWidget*>(widget.getPtr()) : 0;
}

}

ElaraMenuBarWidget::ElaraMenuBarWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    widget_register(root_widget),
    palette_master("tabs"),
    font_size(14),
    item_padding_x(12),
    item_padding_y(7),
    hover_index(-1),
    active_index(-1) {
}

ElaraMenuBarWidget::~ElaraMenuBarWidget() {}

ElaraRootWidget* ElaraMenuBarWidget::rootWidget() const {
    ElaraWidget* cursor = parent;

    while(cursor && cursor->getParent()) {
        cursor = cursor->getParent();
    }

    return cursor ? dynamic_cast<ElaraRootWidget*>(cursor) : 0;
}

void ElaraMenuBarWidget::syncActiveMenu() {
    if(active_index < 0 || active_index >= (int)menus.length()) {
        active_index = -1;
        return;
    }

    MenuPopupWidget* popup = asMenuPopup(menus[active_index].popup);
    if(!popup || !popup->isVisible()) {
        active_index = -1;
    }
}

double ElaraMenuBarWidget::itemWidth(int index, ElaraDrawContext* ctx) const {
    if(index < 0 || index >= (int)menus.length()) {
        return 0;
    }

    double text_width = ctx
        ? ctx->measureTextWidth(menus[index].label, font_size)
        : menus[index].label.length() * font_size * 0.62;

    return text_width + item_padding_x * 2;
}

double ElaraMenuBarWidget::itemOffsetX(int index, ElaraDrawContext* ctx) const {
    double offset = 0;

    for(int i = 0; i < index && i < (int)menus.length(); i++) {
        offset += itemWidth(i, ctx);
    }

    return offset;
}

int ElaraMenuBarWidget::itemAt(double px, ElaraDrawContext* ctx) const {
    if(px < 0) {
        return -1;
    }

    double cursor = 0;

    for(int i = 0; i < (int)menus.length(); i++) {
        double w = itemWidth(i, ctx);
        if(px >= cursor && px <= cursor + w) {
            return i;
        }
        cursor += w;
    }

    return -1;
}

void ElaraMenuBarWidget::closeMenus() {
    for(int i = 0; i < (int)menus.length(); i++) {
        MenuPopupWidget* popup = asMenuPopup(menus[i].popup);
        if(popup) {
            popup->hide();
        }
    }

    active_index = -1;
}

void ElaraMenuBarWidget::openMenu(int index, ElaraDrawContext* ctx) {
    if(index < 0 || index >= (int)menus.length()) {
        closeMenus();
        return;
    }

    ElaraRootWidget* root = rootWidget();
    MenuPopupWidget* popup = asMenuPopup(menus[index].popup);
    if(!root || !popup) {
        return;
    }

    closeMenus();

    double popup_x = getAbsoluteX() + itemOffsetX(index, ctx);
    double popup_y = getAbsoluteY() + height - 1;

    root->pushPopup(popup->getHandle());
    popup->showAt(popup_x, popup_y);
    active_index = index;
}

void ElaraMenuBarWidget::clearMenus() {
    menus.clear();
    clearChildren();
    hover_index = -1;
    active_index = -1;
}

void ElaraMenuBarWidget::addMenu(const String& menu_id, const String& label) {
    if(menu_id.length() <= 0 || label.length() <= 0) {
        return;
    }

    MenuBarItem item;
    item.id = menu_id;
    item.label = label;
    item.popup_handle = handleString(widget_handle) + String(".popup.") + menu_id;
    item.popup = Ref<ElaraWidget>(
        new MenuPopupWidget(
            widget_register,
            ElaraWidgetHandle(item.popup_handle),
            this,
            menu_id
        )
    );

    if(item.popup) {
        addChild(item.popup);
    }

    menus.push(item);
}

void ElaraMenuBarWidget::addMenuItem(
    const String& menu_id,
    const String& item_id,
    const String& label,
    bool enabled
) {
    for(int i = 0; i < (int)menus.length(); i++) {
        if(menus[i].id == menu_id && menus[i].popup) {
            MenuPopupWidget* popup = asMenuPopup(menus[i].popup);
            if(popup) {
                popup->addItem(item_id, label, enabled);
            }
            return;
        }
    }
}

int ElaraMenuBarWidget::menuCount() const {
    return (int)menus.length();
}

String ElaraMenuBarWidget::getActiveMenuId() const {
    if(active_index < 0 || active_index >= (int)menus.length()) {
        return String();
    }

    return menus[active_index].id;
}

void ElaraMenuBarWidget::setFontSize(double size) {
    font_size = size;
}

double ElaraMenuBarWidget::getFontSize() const {
    return font_size;
}

void ElaraMenuBarWidget::onMenuAction(const String& menu_id, const String& item_id) {
    (void)menu_id;
    emitAction(item_id);
    closeMenus();
}

void ElaraMenuBarWidget::draw(ElaraDrawContext* ctx) {
    syncActiveMenu();

    ElaraPaletteTriplet default_colors = colors(palette_master, "default");
    ElaraPaletteTriplet hover_colors = colors(palette_master, "hover");
    ElaraPaletteTriplet active_colors = colors(palette_master, "active");

    ctx->setColor(default_colors.base.r, default_colors.base.g, default_colors.base.b);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(default_colors.accent.r, default_colors.accent.g, default_colors.accent.b);
    ctx->line(0, height - 1, width, height - 1, 1);

    for(int i = 0; i < (int)menus.length(); i++) {
        double item_x = itemOffsetX(i, ctx);
        double item_w = itemWidth(i, ctx);
        bool active = i == active_index;
        bool hover = i == hover_index;
        ElaraPaletteTriplet cell_colors = default_colors;

        if(active) {
            cell_colors = active_colors;
        } else if(hover) {
            cell_colors = hover_colors;
        }

        if(active || hover) {
            ctx->setColor(cell_colors.base.r, cell_colors.base.g, cell_colors.base.b);
            ctx->fillRect(item_x, 0, item_w, height - 1);
        }

        ctx->setColor(cell_colors.text.r, cell_colors.text.g, cell_colors.text.b);
        ctx->drawText(
            item_x + item_padding_x,
            (height / 2.0) + (font_size / 2.0) - 2,
            menus[i].label,
            font_size
        );
    }
}

void ElaraMenuBarWidget::onMouseMove(double px, double py) {
    syncActiveMenu();
    emitMouseMove(px, py);

    int previous_hover = hover_index;
    hover_index = containsLocal(px, py) ? itemAt(px) : -1;

    if(previous_hover != hover_index) {
        emitHoverChanged(hover_index >= 0);
    }

    if(active_index >= 0 && hover_index >= 0 && hover_index != active_index) {
        openMenu(hover_index);
    }
}

void ElaraMenuBarWidget::onMouseDown(int button, double px, double py) {
    syncActiveMenu();
    emitMouseDown(button, px, py);

    if(button != 1) {
        return;
    }

    int index = containsLocal(px, py) ? itemAt(px) : -1;

    if(index < 0) {
        closeMenus();
        return;
    }

    if(active_index == index) {
        closeMenus();
        return;
    }

    openMenu(index);
}

void ElaraMenuBarWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
}

}

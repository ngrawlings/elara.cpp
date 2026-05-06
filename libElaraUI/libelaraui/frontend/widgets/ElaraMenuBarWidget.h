#ifndef ELARA_MENU_BAR_WIDGET_H
#define ELARA_MENU_BAR_WIDGET_H

#include "ElaraPopupWidget.h"

namespace elara {

class ElaraMenuBarWidget : public ElaraWidget {
public:
    class MenuPopupWidget;

private:
    struct MenuBarItem {
        String id;
        String label;
        String popup_handle;
        Ref<ElaraWidget> popup_widget;
        MenuPopupWidget* popup;
    };

    struct PopupRef {
        String handle;
        Ref<ElaraWidget> widget;
        MenuPopupWidget* popup;
    };

    struct AcceleratorBinding {
        String action_id;
        unsigned int keyval;
        unsigned int modifiers;
    };

    struct MenuLayoutMetric {
        double x;
        double width;
    };

    Array<MenuBarItem> menus;
    Array<PopupRef> popup_refs;
    Array<AcceleratorBinding> accelerators;
    Array<MenuLayoutMetric> layout_metrics;
    ElaraWidgetRegister* widget_register;

    String palette_master;
    double font_size;
    double item_padding_x;
    double item_padding_y;
    int hover_index;
    int active_index;
    bool layout_valid;

    ElaraRootWidget* rootWidget() const;
    MenuPopupWidget* popupForIndex(int index) const;
    MenuPopupWidget* popupForHandle(const String& popup_handle) const;
    MenuPopupWidget* createPopup(const String& popup_handle, ElaraWidget* popup_parent, const String& menu_id);
    MenuPopupWidget* topVisiblePopup() const;
    void syncActiveMenu();
    void invalidateLayout();
    void rebuildLayout(ElaraDrawContext* ctx);
    double itemWidth(int index, ElaraDrawContext* ctx = 0) const;
    double itemOffsetX(int index, ElaraDrawContext* ctx = 0) const;
    int itemAt(double px, ElaraDrawContext* ctx = 0) const;
    void closeMenus();
    void openMenu(int index, ElaraDrawContext* ctx = 0);

public:
    ElaraMenuBarWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraMenuBarWidget();

    void clearMenus();
    void addMenu(const String& menu_id, const String& label);
    void addMenuItem(
        const String& menu_id,
        const String& item_id,
        const String& label,
        bool enabled = true,
        const String& shortcut = String()
    );
    void addMenuSeparator(const String& menu_id);
    void addPopupItem(
        const String& popup_handle,
        const String& menu_id,
        const String& item_id,
        const String& label,
        bool enabled = true,
        const String& shortcut = String(),
        bool separator = false,
        const String& submenu_handle = String()
    );
    void addPopupSeparator(const String& popup_handle, const String& menu_id);

    int menuCount() const;
    String getActiveMenuId() const;

    void setFontSize(double size);
    double getFontSize() const;

    void onMenuAction(const String& menu_id, const String& item_id);
    void setPalette(ElaraPalette* widget_palette);

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
    void onKeyDown(unsigned int keyval);
    void onKeyDown(unsigned int keyval, unsigned int modifiers);
    bool dispatchAccelerator(unsigned int keyval, unsigned int modifiers);
};

}

#endif

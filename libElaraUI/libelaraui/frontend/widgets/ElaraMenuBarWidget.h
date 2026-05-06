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
        Ref<ElaraWidget> popup;
    };

    Array<MenuBarItem> menus;
    ElaraWidgetRegister* widget_register;

    String palette_master;
    double font_size;
    double item_padding_x;
    double item_padding_y;
    int hover_index;
    int active_index;

    ElaraRootWidget* rootWidget() const;
    void syncActiveMenu();
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
        bool enabled = true
    );

    int menuCount() const;
    String getActiveMenuId() const;

    void setFontSize(double size);
    double getFontSize() const;

    void onMenuAction(const String& menu_id, const String& item_id);

    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

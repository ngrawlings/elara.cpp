#ifndef ELARA_POPUP_WIDGET_H
#define ELARA_POPUP_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraPopupItem {
private:
    String id;
    String text;
    String shortcut;
    bool enabled;
    bool separator;
    String submenu_handle;
    Ref<ElaraWidget> submenu_widget;

public:
    ElaraPopupItem();
    ElaraPopupItem(const String& item_id, const String& item_text);

    String getId() const;
    String getText() const;
    String getShortcut() const;
    void setShortcut(const String& value);

    bool isEnabled() const;
    void setEnabled(bool value);
    bool isSeparator() const;
    void setSeparator(bool value);
    bool hasSubmenu() const;
    String getSubmenuHandle() const;
    Ref<ElaraWidget> getSubmenuWidget() const;
    void setSubmenu(const String& handle, Ref<ElaraWidget> widget);
};

class ElaraPopupWidget : public ElaraWidget {
private:
    Array<ElaraPopupItem> items;

    bool visible;
    int hover_index;

    double item_height;
    double padding;

    int itemAt(double px, double py) const;
    double itemTop(int index) const;
    ElaraRootWidget* rootWidget() const;
    void closeDescendantPopups();
    void openSubmenu(int index);
    int firstSelectableIndex() const;
    int moveSelection(int start, int delta) const;
    int selectedIndex() const;

public:
    ElaraPopupWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);

    void showAt(double px, double py);
    void hide();
    bool isVisible() const;

    void clearItems();
    void addItem(
        const String& id,
        const String& text,
        bool enabled = true,
        bool separator = false,
        const String& shortcut = String(),
        const String& submenu_handle = String(),
        Ref<ElaraWidget> submenu_widget = Ref<ElaraWidget>(0)
    );
    int itemCount() const;
    void selectFirstItem();
    void selectLastItem();
    void clearSelection();
    bool hasParentPopup() const;

    virtual void onItemSelected(const String& id) {}

    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
    void onKeyDown(unsigned int keyval);
};

}

#endif

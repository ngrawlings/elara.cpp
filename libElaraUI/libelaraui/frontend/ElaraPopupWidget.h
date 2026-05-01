#ifndef ELARA_POPUP_WIDGET_H
#define ELARA_POPUP_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraPopupItem {
private:
    String id;
    String text;
    bool enabled;

public:
    ElaraPopupItem();
    ElaraPopupItem(const String& item_id, const String& item_text);

    String getId() const;
    String getText() const;

    bool isEnabled() const;
    void setEnabled(bool value);
};

class ElaraPopupWidget : public ElaraWidget {
private:
    Array<ElaraPopupItem> items;

    bool visible;
    int hover_index;

    double item_height;
    double padding;

    int itemAt(double px, double py) const;

public:
    ElaraPopupWidget(ElaraWidgetRegister* root_widget, ElaraWidgetHandle widget_handle);

    void showAt(double px, double py);
    void hide();
    bool isVisible() const;

    void clearItems();
    void addItem(const String& id, const String& text);

    virtual void onItemSelected(const String& id) {}

    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

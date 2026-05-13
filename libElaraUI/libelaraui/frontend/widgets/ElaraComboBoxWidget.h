#ifndef ELARA_COMBO_BOX_WIDGET_H
#define ELARA_COMBO_BOX_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraComboItem {
private:
    String id;
    String text;

public:
    ElaraComboItem();
    ElaraComboItem(const String& item_id, const String& item_text);

    String getId() const;
    String getText() const;
};

class ElaraComboBoxWidget : public ElaraWidget {
private:
    Array<ElaraComboItem> items;

    String selected_id;
    String selected_text;

    bool enabled;
    bool hovered;
    bool pressed;

    double font_size;
    double arrow_width;

    ElaraWidgetHandle dropdown_handle;

    ElaraRootWidget* rootWidget() const;
    void openDropdown();

public:
    ElaraComboBoxWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraComboBoxWidget();

    void clearItems();
    void addItem(const String& id, const String& item_text);
    int getItemCount() const;

    void setSelectedId(const String& id);
    void setSelectedText(const String& text);
    String getSelectedId() const;
    String getSelectedText() const;

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double value);

    void onDropdownItemSelected(const String& id);

    void draw(ElaraDrawContext* ctx);
    ElaraMouseCursor cursor() const;

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);

    bool wantsFocus() const;
};

}

#endif

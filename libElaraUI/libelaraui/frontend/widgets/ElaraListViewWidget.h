#ifndef ELARA_LIST_VIEW_WIDGET_H
#define ELARA_LIST_VIEW_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

class ElaraListViewItem {
private:
    String id;
    String text;

public:
    ElaraListViewItem();
    ElaraListViewItem(const String& item_id, const String& item_text);

    void setId(const String& value);
    String getId() const;

    void setText(const String& value);
    String getText() const;
};

class ElaraListViewWidget : public ElaraWidget {
private:
    Array<ElaraListViewItem> items;

    String palette_master;
    String selected_id;
    String selected_text;

    bool enabled;
    int hover_index;
    double font_size;
    double row_height;
    double padding_x;

    int rowAt(double py) const;

public:
    ElaraListViewWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraListViewWidget();

    void clearItems();
    void addItem(const ElaraListViewItem& item);

    void setEnabled(bool value);
    bool isEnabled() const;

    void setFontSize(double value);
    double getFontSize() const;

    String getSelectedId() const;
    String getSelectedText() const;
    int getItemCount() const;

    bool acceptsDoubleClick() const;
    ElaraMouseCursor cursor() const;
    void draw(ElaraDrawContext* ctx);
    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
    void onMouseDoubleClick(int button, double px, double py);
};

}

#endif

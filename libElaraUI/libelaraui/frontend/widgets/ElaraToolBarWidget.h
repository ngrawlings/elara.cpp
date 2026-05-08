#ifndef ELARA_TOOL_BAR_WIDGET_H
#define ELARA_TOOL_BAR_WIDGET_H

#include "ElaraWidget.h"

namespace elara {

enum ElaraToolBarOrientation {
    ELARA_TOOLBAR_HORIZONTAL,
    ELARA_TOOLBAR_VERTICAL
};

class ElaraToolBarWidget : public ElaraWidget {
private:
    struct ToolBarItem {
        String id;
        String text;
        String icon;
        String tooltip;
        bool enabled;
        bool separator;
    };

    struct ToolBarLayoutMetric {
        double x;
        double y;
        double width;
        double height;
    };

    Array<ToolBarItem> items;
    Array<ToolBarLayoutMetric> layout_metrics;

    ElaraToolBarOrientation orientation;
    String palette_master;
    double font_size;
    double item_padding_x;
    double item_padding_y;
    double item_spacing;
    double separator_size;
    double icon_text_gap;
    int hover_index;
    int pressed_index;
    bool layout_valid;

    void invalidateLayout();
    String displayTextForItem(int index) const;
    double estimatedTextWidth(const String& value) const;
    void itemPreferredSize(int index, ElaraDrawContext* ctx, double* out_w, double* out_h) const;
    void rebuildLayout(ElaraDrawContext* ctx);
    int itemAt(double px, double py, ElaraDrawContext* ctx = 0) const;
    bool itemIsActionable(int index) const;

public:
    ElaraToolBarWidget(
        ElaraWidgetRegister* widget_register,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraToolBarWidget();

    void clearItems();
    void addItem(
        const String& item_id,
        const String& text,
        const String& icon = String(),
        bool enabled = true,
        const String& tooltip = String()
    );
    void addSeparator();

    int itemCount() const;
    String getItemId(int index) const;

    void setOrientation(ElaraToolBarOrientation value);
    ElaraToolBarOrientation getOrientation() const;
    void setHorizontal(bool horizontal);
    bool isHorizontal() const;

    void setFontSize(double size);
    double getFontSize() const;
    void setItemPadding(double x, double y);
    void setItemSpacing(double value);

    void setItemEnabled(const String& item_id, bool enabled);
    bool isItemEnabled(const String& item_id) const;

    ElaraMouseCursor cursor() const;
    ElaraMouseCursor cursorAt(double px, double py) const;
    void draw(ElaraDrawContext* ctx);

    void onMouseMove(double px, double py);
    void onMouseDown(int button, double px, double py);
    void onMouseUp(int button, double px, double py);
};

}

#endif

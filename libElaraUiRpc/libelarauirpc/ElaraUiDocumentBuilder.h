#ifndef ELARA_UI_DOCUMENT_BUILDER_H
#define ELARA_UI_DOCUMENT_BUILDER_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiDocumentBuilder {
private:
    struct JsonField {
        String name;
        String json_value;
    };

    struct ChildRef {
        String child_id;
    };

    struct GridPlacement {
        String child_id;
        int column;
        int row;
        int column_span;
        int row_span;
    };

    struct TabRef {
        String title;
        String child_id;
    };

    struct PopupItem {
        String id;
        String label;
    };

    struct GridTrack {
        bool fill;
        double size;
    };

    struct WidgetSpec {
        String id;
        String type;
        Array<JsonField> properties;
        Array<JsonField> sections;
        Array<ChildRef> children;
        Array<GridPlacement> grid_children;
        Array<TabRef> tabs;
        Array<PopupItem> popup_items;
        Array<GridTrack> grid_columns;
        Array<GridTrack> grid_rows;
    };

    String window_title;
    int window_width;
    int window_height;
    String window_backend_id;
    String theme_mode;
    String root_content;
    Array<String> root_popups;
    Array<WidgetSpec> widgets;

    int findWidgetIndex(const String& id) const;
    WidgetSpec* getWidgetSpec(const String& id);
    const WidgetSpec* getWidgetSpec(const String& id) const;

    void setField(Array<JsonField>* fields, const String& name, const String& json_value);
    void detachChildReference(const String& child_id);
    bool isNestedWidget(const String& id) const;

    String serializeFields(const Array<JsonField>& fields) const;
    String serializeWidget(const WidgetSpec& spec) const;

public:
    ElaraUiDocumentBuilder();
    virtual ~ElaraUiDocumentBuilder();

    void clear();

    void createWindow(
        const String& title,
        int width,
        int height,
        const String& backend_id
    );
    void setThemeMode(const String& mode);
    void setRootContent(const String& widget_id);
    void clearRootPopups();
    void pushRootPopup(const String& widget_id);

    bool hasWidget(const String& id) const;
    bool createWidget(const String& id, const String& type);
    bool createTabs(const String& id);
    bool createPopup(const String& id);
    bool createMenuBar(const String& id);
    bool createGrid(const String& id);
    bool createButton(const String& id, const String& text, const String& action);
    bool createCheckbox(const String& id, const String& text, bool checked);
    bool createRadioButton(const String& id, const String& text, const String& group, bool checked);
    bool createLabel(const String& id, const String& text, double font_size);
    bool createTextInput(const String& id, const String& placeholder, const String& text);
    bool createSlider(
        const String& id,
        const String& orientation,
        double min_value,
        double max_value,
        double value,
        double step
    );
    bool createSpinner(
        const String& id,
        double min_value,
        double max_value,
        double value,
        double step
    );
    bool createListView(const String& id);
    bool createTreeView(const String& id);
    bool createRichTextEdit(const String& id, const String& text);
    bool createSurfacePanel(const String& id);
    bool createDensityMap(const String& id);
    bool createMultiAxisLineChart(const String& id);

    bool addChild(const String& parent_id, const String& child_id);
    bool addTab(const String& tabs_id, const String& title, const String& child_id);
    bool addPopupItem(const String& popup_id, const String& item_id, const String& label);
    bool addMenuBarMenu(const String& menu_bar_id, const String& menu_id, const String& label);
    bool addMenuBarSeparator(const String& menu_bar_id, const String& menu_id);
    bool addMenuBarItem(
        const String& menu_bar_id,
        const String& menu_id,
        const String& item_id,
        const String& label,
        bool enabled = true,
        const String& shortcut = String()
    );
    bool addGridColumnExact(const String& grid_id, double size);
    bool addGridColumnFill(const String& grid_id);
    bool addGridRowExact(const String& grid_id, double size);
    bool addGridRowFill(const String& grid_id);
    bool placeGridChild(
        const String& grid_id,
        const String& child_id,
        int column,
        int row,
        int column_span = 1,
        int row_span = 1
    );

    bool setPropertyString(const String& widget_id, const String& name, const String& value);
    bool setPropertyNumber(const String& widget_id, const String& name, double value);
    bool setPropertyBool(const String& widget_id, const String& name, bool value);
    bool setPropertyJson(const String& widget_id, const String& name, const String& raw_json);
    bool setSectionJson(const String& widget_id, const String& section_name, const String& raw_json);

    String toJson() const;
    String widgetJson(const String& widget_id) const;
};

}
}
}

#endif

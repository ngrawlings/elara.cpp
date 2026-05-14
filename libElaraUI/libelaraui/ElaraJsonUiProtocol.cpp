#include "ElaraJsonUiProtocol.h"

#include <stdio.h>
#include <string.h>
#include <libelaraio/File.h>
#include <libelaraformat/json/types/JsonValue.h>
#include <libelaravector/elara_vector.h>
#include <libelaravectorcpp/ElaraVectorDocument.h>

#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>
#include <libelaraui/frontend/layouts/ElaraGridLayout.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraCheckboxWidget.h>
#include <libelaraui/frontend/widgets/ElaraMenuBarWidget.h>
#include <libelaraui/frontend/widgets/ElaraRadioButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraRichTextEditWidget.h>
#include <libelaraui/frontend/widgets/ElaraChatDialogWidget.h>
#include <libelaraui/frontend/widgets/ElaraCodeEditorWidget.h>
#include <libelaraui/frontend/widgets/ElaraToolBarWidget.h>
#include <libelaraui/frontend/widgets/ElaraSliderWidget.h>
#include <libelaraui/frontend/widgets/ElaraSpinnerWidget.h>
#include <libelaraui/frontend/widgets/ElaraListViewWidget.h>
#include <libelaraui/frontend/widgets/ElaraLabelWidget.h>
#include <libelaraui/frontend/widgets/ElaraTextInputWidget.h>
#include <libelaraui/frontend/widgets/ElaraTreeViewWidget.h>
#include <libelaraui/frontend/widgets/ElaraComboBoxWidget.h>
#include <libelaraui/frontend/widgets/ElaraOpenClSurfaceWidget.h>
#include <libelaraui/frontend/widgets/instruments/ElaraDensityMapWidget.h>
#include <libelaraui/frontend/widgets/instruments/ElaraMultiAxisLineChartWidget.h>

namespace elara {

static int jsonInt(const Json& json, const String& path, int fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value || value->getType() == JsonValue::INVALID) {
        return fallback;
    }

    return json.getIntValue(path);
}

static double jsonDouble(const Json& json, const String& path, double fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value || value->getType() == JsonValue::INVALID) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text.length() <= 0) {
        return fallback;
    }

    return atof((const char*)text);
}

static double jsonValueDouble(Ref<JsonValue> value, double fallback) {
    if(!value || value->getType() == JsonValue::INVALID) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text.length() <= 0) {
        return fallback;
    }

    return atof((const char*)text);
}

static String jsonString(const Json& json, const String& path, const String& fallback) {
    String value = json.getStringValue(path);

    if(value.length() <= 0) {
        return fallback;
    }

    return value;
}

static bool jsonBool(const Json& json, const String& path, bool fallback) {
    Ref<JsonValue> value = json.getJsonValue(path);

    if(!value || value->getType() == JsonValue::INVALID) {
        return fallback;
    }

    String text = value->toString().trim();

    if(text == String("true") || text == String("\"true\"")) {
        return true;
    }

    if(text == String("false") || text == String("\"false\"")) {
        return false;
    }

    return fallback;
}

static String handleString(const ElaraWidgetHandle& handle) {
    Memory memory = handle.getHandle();
    return String((const char*)memory.getPtr(), memory.length());
}

static ElaraTreeViewNode parseTreeNodeSpec(const Json& spec) {
    ElaraTreeViewNode node(spec.getStringValue("id"), spec.getStringValue("label"));
    node.setExpanded(jsonBool(spec, "expanded", false));
    Array< Ref<JsonValue> > buttons = spec.getArray("buttons");
    for(int i = 0; i < (int)buttons.length(); i++) {
        Json btn(buttons[i]->toString());
        ElaraTreeViewNodeButton button;
        button.glyph  = btn.getStringValue("glyph");
        button.action = btn.getStringValue("action");
        node.addButton(button);
    }
    Array< Ref<JsonValue> > children = spec.getArray("children");
    for(int i = 0; i < (int)children.length(); i++) {
        Json child_json(children[i]->toString());
        node.addChild(parseTreeNodeSpec(child_json));
    }
    return node;
}

class ElaraJsonLabelWidget : public ElaraLabelWidget {
public:
    ElaraJsonLabelWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraLabelWidget(root, handle) {}

    void draw(ElaraDrawContext* ctx) {
        ElaraPaletteTriplet c = colors("panel", "default");
        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);
        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->drawText(12, (height / 2) + (getFontSize() / 2) - 2, getText(), getFontSize());
    }
};

class ElaraJsonTextInputWidget : public ElaraWidget {
private:
    String value;
    String placeholder;
    double font_size;
    bool enabled;

public:
    ElaraJsonTextInputWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraWidget(root, handle),
          value(""),
          placeholder("Text input"),
          font_size(14),
          enabled(true) {}

    void setText(const String& text_value) {
        value = text_value;
    }

    void setPlaceholder(const String& text_value) {
        placeholder = text_value;
    }

    void setFontSize(double size) {
        font_size = size;
    }

    void setEnabled(bool input_enabled) {
        enabled = input_enabled;
    }

    void draw(ElaraDrawContext* ctx) {
        String sub("default");

        if(!enabled) {
            sub = String("disabled");
        }

        ElaraPaletteTriplet c = colors("input", sub);

        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);

        ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
        ctx->line(0, 0, width, 0, 1);
        ctx->line(0, height - 1, width, height - 1, 1);
        ctx->line(0, 0, 0, height, 1);
        ctx->line(width - 1, 0, width - 1, height, 1);

        ctx->setColor(c.text.r, c.text.g, c.text.b);

        if(value.length() > 0) {
            ctx->drawText(10, (height / 2) + (font_size / 2) - 2, value, font_size);
        } else {
            ctx->drawText(10, (height / 2) + (font_size / 2) - 2, placeholder, font_size);
        }
    }
};

class ElaraJsonSurfacePanelWidget : public ElaraWidget {
private:
    double mouse_x;
    double mouse_y;
    bool mouse_down;

public:
    ElaraJsonSurfacePanelWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraWidget(root, handle),
          mouse_x(400),
          mouse_y(300),
          mouse_down(false) {}

    void draw(ElaraDrawContext* ctx) {
        ctx->clear(0.08, 0.08, 0.10);
        ctx->setColor(0.25, 0.25, 0.30);
        ctx->fillRect(20, 20, width - 40, height - 40);

        if(mouse_down) {
            ctx->setColor(0.95, 0.65, 0.20);
        } else {
            ctx->setColor(0.80, 0.80, 0.90);
        }

        ctx->fillCircle(mouse_x, mouse_y, 24);
        ctx->setColor(1.0, 1.0, 1.0);
        ctx->line(0, 0, mouse_x, mouse_y, 2);
    }

    void onMouseMove(double px, double py) {
        mouse_x = px;
        mouse_y = py;
    }

    void onMouseDown(int button, double px, double py) {
        mouse_down = true;
        mouse_x = px;
        mouse_y = py;
        printf("mouse down: button=%d x=%f y=%f\n", button, px, py);
    }

    void onMouseUp(int button, double px, double py) {
        mouse_down = false;
        printf("mouse up: button=%d x=%f y=%f\n", button, px, py);
    }
};

class ElaraBuiltinWidgetFactory : public ElaraJsonWidgetFactory {
private:
    void applyMenuBarItems(
        ElaraMenuBarWidget* menu_bar,
        const String& menu_id,
        const String& popup_handle,
        const Json& spec
    ) {
        Array< Ref<JsonValue> > items = spec.getArray("items");

        for(int i = 0; i < (int)items.length(); i++) {
            Json item(items[i]);
            bool separator = jsonBool(item, "separator", false)
                || item.getStringValue("type") == String("separator");
            String item_id = item.getStringValue("id");
            String item_label = item.getStringValue("label");
            String shortcut = item.getStringValue("shortcut");
            bool enabled = jsonString(item, "enabled", String("true")) != String("false");
            Array< Ref<JsonValue> > submenu_items = item.getArray("items");
            String submenu_handle;

            if(separator) {
                menu_bar->addPopupSeparator(popup_handle, menu_id);
                continue;
            }

            if(item_id.length() <= 0 || item_label.length() <= 0) {
                continue;
            }

            if(submenu_items.length() > 0) {
                submenu_handle = popup_handle + String(".submenu.") + item_id;
            }

            menu_bar->addPopupItem(
                popup_handle,
                menu_id,
                item_id,
                item_label,
                enabled,
                shortcut,
                false,
                submenu_handle
            );

            if(submenu_handle.length() > 0) {
                applyMenuBarItems(menu_bar, menu_id, submenu_handle, item);
            }
        }
    }

    void applyToolbarItems(ElaraToolBarWidget* toolbar, const Json& spec) {
        toolbar->clearItems();

        Array< Ref<JsonValue> > items = spec.getArray("items");

        for(int i = 0; i < (int)items.length(); i++) {
            Json item(items[i]);
            bool separator = jsonBool(item, "separator", false)
                || item.getStringValue("type") == String("separator");

            if(separator) {
                toolbar->addSeparator();
                continue;
            }

            String id = item.getStringValue("id");
            String text = item.getStringValue("text");
            String icon = item.getStringValue("icon");
            bool enabled = jsonString(item, "enabled", String("true")) != String("false");
            String tooltip = item.getStringValue("tooltip");

            if(id.length() > 0) {
                toolbar->addItem(id, text, icon, enabled, tooltip);
            }
        }
    }

    void applyListItems(ElaraListViewWidget* list, const Json& spec) {
        list->clearItems();

        Array< Ref<JsonValue> > items = spec.getArray("items");

        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            list->addItem(
                ElaraListViewItem(
                    item_json.getStringValue("id"),
                    item_json.getStringValue("label")
                )
            );
        }
    }

    ElaraTreeViewNode parseTreeNode(const Json& spec) {
        ElaraTreeViewNode node(
            spec.getStringValue("id"),
            spec.getStringValue("label")
        );

        node.setExpanded(jsonBool(spec, "expanded", false));

        Array< Ref<JsonValue> > buttons = spec.getArray("buttons");
        for(int i = 0; i < (int)buttons.length(); i++) {
            Json btn(buttons[i]->toString());
            ElaraTreeViewNodeButton button;
            button.glyph  = btn.getStringValue("glyph");
            button.action = btn.getStringValue("action");
            node.addButton(button);
        }

        Array< Ref<JsonValue> > children = spec.getArray("children");

        for(int i = 0; i < (int)children.length(); i++) {
            Json child_json(children[i]->toString());
            node.addChild(parseTreeNode(child_json));
        }

        return node;
    }

    void applyTreeNodes(ElaraTreeViewWidget* tree, const Json& spec) {
        tree->clearNodes();

        Array< Ref<JsonValue> > nodes = spec.getArray("nodes");

        for(int i = 0; i < (int)nodes.length(); i++) {
            Json node_json(nodes[i]->toString());
            tree->addRootNode(parseTreeNode(node_json));
        }
    }

    ElaraWidget* createChildWidget(ElaraWidgetRegister* root, const Json& spec) {
        String id = spec.getStringValue("id");
        String type = spec.getStringValue("type");

        if(id.length() <= 0 || type.length() <= 0) {
            printf("json ui: child widget requires id and type\n");
            return 0;
        }

        return createWidget(root, id, spec);
    }

    void applyPopupItems(ElaraPopupWidget* popup, const Json& spec) {
        Array< Ref<JsonValue> > items = spec.getArray("items");

        for(int i = 0; i < (int)items.length(); i++) {
            Json item(items[i]);
            bool separator = jsonBool(item, "separator", false)
                || item.getStringValue("type") == String("separator");
            String id = item.getStringValue("id");
            String label = item.getStringValue("label");
            String shortcut = item.getStringValue("shortcut");
            bool enabled = jsonString(item, "enabled", String("true")) != String("false");

            if(separator) {
                popup->addItem(String("__separator__"), String(), false, true, String());
                continue;
            }

            if(id.length() > 0 && label.length() > 0) {
                popup->addItem(id, label, enabled, false, shortcut);
            }
        }
    }

    void applyMenuBarButtons(ElaraMenuBarWidget* menu_bar, const Json& spec) {
        if(!menu_bar) {
            return;
        }

        Array< Ref<JsonValue> > btn_specs = spec.getArray("buttons");
        for(int i = 0; i < (int)btn_specs.length(); i++) {
            Json btn(btn_specs[i]->toString());
            String id     = btn.getStringValue("id");
            String glyph  = btn.getStringValue("glyph");
            String action = btn.getStringValue("action");
            if(id.length() > 0) {
                menu_bar->addButton(id, glyph, action);
            }
        }
    }

    void applyMenuBarMenus(ElaraMenuBarWidget* menu_bar, const Json& spec) {
        if(!menu_bar) {
            return;
        }

        menu_bar->clearMenus();

        Array< Ref<JsonValue> > menu_specs = spec.getArray("menus");

        for(int i = 0; i < (int)menu_specs.length(); i++) {
            Json menu_spec(menu_specs[i]);
            String id = menu_spec.getStringValue("id");
            String label = menu_spec.getStringValue("label");

            if(id.length() <= 0 || label.length() <= 0) {
                continue;
            }

            menu_bar->addMenu(id, label);
            String popup_handle = id.length() > 0
                ? handleString(menu_bar->getHandle()) + String(".popup.") + id
                : String();

            applyMenuBarItems(menu_bar, id, popup_handle, menu_spec);
        }
    }

    void applyTabs(ElaraWidgetRegister* root, ElaraTabWidget* tabs, const Json& spec) {
        Array< Ref<JsonValue> > tab_specs = spec.getArray("tabs");

        for(int i = 0; i < (int)tab_specs.length(); i++) {
            Json tab_spec(tab_specs[i]);
            String title = jsonString(tab_spec, "title", String("Tab"));
            Ref<JsonValue> widget_value = tab_spec.getJsonValue("widget");

            if(!widget_value || widget_value->getType() == JsonValue::INVALID) {
                printf("json ui: tab %s has no widget\n", (const char*)title);
                continue;
            }

            Json widget_spec(widget_value);
            ElaraWidget* child = createChildWidget(root, widget_spec);

            if(!child) {
                printf("json ui: failed to create tab widget %s\n", (const char*)title);
                continue;
            }

            String btn_glyph = tab_spec.getStringValue("button_glyph");
            String btn_action = tab_spec.getStringValue("button_action");
            tabs->addTab(title, child, btn_glyph, btn_action);
        }
    }

    void applyGridTracks(ElaraGridLayout* grid, const Json& spec) {
        Array< Ref<JsonValue> > columns = spec.getArray("columns");

        for(int i = 0; i < (int)columns.length(); i++) {
            Json column(columns[i]);
            String mode = column.getStringValue("mode");

            if(mode == String("fill")) {
                double weight = (double)jsonInt(column, "weight", 1);
                if(weight > 0 && weight != 1.0) {
                    grid->addWeightedFillColumn(weight);
                } else {
                    grid->addFillColumn();
                }
            } else {
                grid->addColumn((double)jsonInt(column, "size", 0));
            }

            if(jsonBool(column, "resizable", false)) {
                grid->setColumnBorderResizable(i, true);
            }
        }

        Array< Ref<JsonValue> > rows = spec.getArray("rows");

        for(int i = 0; i < (int)rows.length(); i++) {
            Json row(rows[i]);
            String mode = row.getStringValue("mode");

            if(mode == String("fill")) {
                double weight = (double)jsonInt(row, "weight", 1);
                if(weight > 0 && weight != 1.0) {
                    grid->addWeightedFillRow(weight);
                } else {
                    grid->addFillRow();
                }
            } else {
                grid->addRow((double)jsonInt(row, "size", 0));
            }

            if(jsonBool(row, "resizable", false)) {
                grid->setRowBorderResizable(i, true);
            }
        }
    }

    void applyGridChildren(ElaraWidgetRegister* root, ElaraGridLayout* grid, const Json& spec) {
        Array< Ref<JsonValue> > children = spec.getArray("children");

        for(int i = 0; i < (int)children.length(); i++) {
            Json child_spec(children[i]);
            ElaraWidget* child = createChildWidget(root, child_spec);

            if(!child) {
                continue;
            }

            String id = child_spec.getStringValue("id");
            int column = jsonInt(child_spec, "cell.column", 0);
            int row = jsonInt(child_spec, "cell.row", 0);
            int column_span = jsonInt(child_spec, "cell.column_span", 1);
            int row_span = jsonInt(child_spec, "cell.row_span", 1);

            grid->addWidget(id, column, row, column_span, row_span);
            grid->addChild(Ref<ElaraWidget>(child));
        }
    }

    void applyLineChartDemoData(ElaraMultiAxisLineChartWidget* chart, const Json& spec) {
        if(!chart) {
            return;
        }

        Array< Ref<JsonValue> > axis_specs = spec.getArray("demo_data.axes");
        chart->clearAxes();

        for(int i = 0; i < (int)axis_specs.length(); i++) {
            Json axis_spec(axis_specs[i]);
            ElaraChartAxis axis;

            axis.id = jsonString(axis_spec, "id", String(""));
            axis.label = jsonString(axis_spec, "label", axis.id);
            axis.side = jsonString(axis_spec, "side", String("left"));
            axis.minimum = jsonDouble(axis_spec, "min", 0.0);
            axis.maximum = jsonDouble(axis_spec, "max", 100.0);

            Array< Ref<JsonValue> > color = axis_spec.getArray("color");
            if(color.length() >= 3) {
                axis.r = jsonValueDouble(color[0], axis.r);
                axis.g = jsonValueDouble(color[1], axis.g);
                axis.b = jsonValueDouble(color[2], axis.b);
            }

            chart->addAxis(axis);
        }

        Array< Ref<JsonValue> > series_specs = spec.getArray("demo_data.series");
        chart->clearSeries();

        for(int i = 0; i < (int)series_specs.length(); i++) {
            Json series_spec(series_specs[i]);
            ElaraChartSeries series;

            series.id = jsonString(series_spec, "id", String(""));
            series.label = jsonString(series_spec, "label", series.id);
            series.axis_id = jsonString(series_spec, "axis", String(""));

            Array< Ref<JsonValue> > color = series_spec.getArray("color");
            if(color.length() >= 3) {
                series.r = jsonValueDouble(color[0], series.r);
                series.g = jsonValueDouble(color[1], series.g);
                series.b = jsonValueDouble(color[2], series.b);
            }

            Array< Ref<JsonValue> > values = series_spec.getArray("values");
            for(int value_index = 0; value_index < (int)values.length(); value_index++) {
                series.values.push(jsonValueDouble(values[value_index], 0.0));
            }

            chart->addSeries(series);
        }
    }

public:
    bool supports(const String& type) const {
        return
            type == String("elara.widgets.tabs") ||
            type == String("elara.widgets.popup") ||
            type == String("elara.widgets.menu_bar") ||
            type == String("elara.layouts.grid") ||
            type == String("elara.widgets.button") ||
            type == String("elara.widgets.checkbox") ||
            type == String("elara.widgets.radio_button") ||
            type == String("elara.widgets.label") ||
            type == String("elara.widgets.rich_text_edit") ||
            type == String("elara.widgets.code_editor") ||
            type == String("elara.widgets.toolbar") ||
            type == String("elara.widgets.slider") ||
            type == String("elara.widgets.spinner") ||
            type == String("elara.widgets.text_input") ||
            type == String("elara.widgets.list_view") ||
            type == String("elara.widgets.tree_view") ||
            type == String("elara.widgets.surface_panel") ||
            type == String("elara.widgets.opencl_surface") ||
            type == String("elara.widgets.density_map") ||
            type == String("elara.widgets.multi_axis_line_chart") ||
            type == String("elara.widgets.combo_box") ||
            type == String("elara.widgets.chat_dialog") ||
            type == String("demo.widgets.button") ||
            type == String("demo.widgets.label") ||
            type == String("demo.widgets.text_input") ||
            type == String("demo.widgets.surface_panel");
    }

    ElaraWidget* createWidget(
        ElaraWidgetRegister* root,
        const String& id,
        const Json& spec
    ) {
        String type = spec.getStringValue("type");
        ElaraWidget* widget = 0;

        if(type == String("elara.widgets.tabs")) {
            ElaraTabWidget* tabs = new ElaraTabWidget(root, id);
            applyTabs(root, tabs, spec);
            widget = tabs;
        } else if(type == String("elara.widgets.popup")) {
            ElaraPopupWidget* popup = new ElaraPopupWidget(root, id);
            applyPopupItems(popup, spec);
            widget = popup;
        } else if(type == String("elara.widgets.menu_bar")) {
            widget = new ElaraMenuBarWidget(root, id);
        } else if(type == String("elara.layouts.grid")) {
            ElaraGridLayout* grid = new ElaraGridLayout(root, id);
            applyGridTracks(grid, spec);
            applyGridChildren(root, grid, spec);
            widget = grid;
        } else if(type == String("elara.widgets.button") || type == String("demo.widgets.button")) {
            widget = new ElaraButtonWidget(root, id);
        } else if(type == String("elara.widgets.checkbox")) {
            widget = new ElaraCheckboxWidget(root, id);
        } else if(type == String("elara.widgets.radio_button")) {
            widget = new ElaraRadioButtonWidget(root, id);
        } else if(type == String("elara.widgets.slider")) {
            widget = new ElaraSliderWidget(root, id);
        } else if(type == String("elara.widgets.spinner")) {
            widget = new ElaraSpinnerWidget(root, id);
        } else if(type == String("elara.widgets.list_view")) {
            ElaraListViewWidget* list = new ElaraListViewWidget(root, id);
            applyListItems(list, spec);
            widget = list;
        } else if(type == String("elara.widgets.tree_view")) {
            ElaraTreeViewWidget* tree = new ElaraTreeViewWidget(root, id);
            applyTreeNodes(tree, spec);
            widget = tree;
        } else if(type == String("elara.widgets.rich_text_edit")) {
            widget = new ElaraRichTextEditWidget(root, id);
        } else if(type == String("elara.widgets.code_editor")) {
            widget = new ElaraCodeEditorWidget(root, id);
        } else if(type == String("elara.widgets.toolbar")) {
            ElaraToolBarWidget* toolbar = new ElaraToolBarWidget(root, id);
            String tb_orientation = spec.getStringValue("properties.orientation");
            if(tb_orientation == String("vertical")) {
                toolbar->setOrientation(ELARA_TOOLBAR_VERTICAL);
            }
            applyToolbarItems(toolbar, spec);
            widget = toolbar;
        } else if(type == String("elara.widgets.label") || type == String("demo.widgets.label")) {
            widget = new ElaraJsonLabelWidget(root, id);
        } else if(type == String("elara.widgets.text_input") || type == String("demo.widgets.text_input")) {
            widget = new ElaraTextInputWidget(root, id);
        } else if(type == String("elara.widgets.surface_panel") || type == String("demo.widgets.surface_panel")) {
            widget = new ElaraJsonSurfacePanelWidget(root, id);
        } else if(type == String("elara.widgets.opencl_surface")) {
            widget = new ElaraOpenClSurfaceWidget(root, id);
        } else if(type == String("elara.widgets.density_map")) {
            widget = new ElaraDensityMapWidget(root, id);
            // TODO: apply styling attribgutes within the json spec
        } else if(type == String("elara.widgets.multi_axis_line_chart")) {
            widget = new ElaraMultiAxisLineChartWidget(root, id);
        } else if(type == String("elara.widgets.combo_box")) {
            widget = new ElaraComboBoxWidget(root, id);
        } else if(type == String("elara.widgets.chat_dialog")) {
            widget = new ElaraChatDialogWidget(root, id);
        }

        applyProperties(widget, spec);
        return widget;
    }

    void applyProperties(ElaraWidget* widget, const Json& spec) {
        if(!widget) {
            return;
        }

        widget->setVisible(
            jsonString(spec, "properties.visible", String("true")) == String("false")
                ? false
                : true
        );

        ElaraButtonWidget* button = dynamic_cast<ElaraButtonWidget*>(widget);
        if(button) {
            String text = spec.getStringValue("properties.text");
            String action = spec.getStringValue("properties.action");

            if(text.length() > 0) {
                button->setText(text);
            }

            if(action.length() > 0) {
                button->setAction(action);
            }

            button->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) == String("false")
                    ? false
                    : true
            );

            double font_size = jsonDouble(spec, "properties.font_size", -1.0);
            if(font_size > 0) button->setFontSize(font_size);

            double pad_all   = jsonDouble(spec, "properties.padding",        -1.0);
            double pad_x     = jsonDouble(spec, "properties.padding_x",      -1.0);
            double pad_y     = jsonDouble(spec, "properties.padding_y",      -1.0);
            double pad_left  = jsonDouble(spec, "properties.padding_left",   -1.0);
            double pad_right = jsonDouble(spec, "properties.padding_right",  -1.0);
            double pad_top   = jsonDouble(spec, "properties.padding_top",    -1.0);
            double pad_bot   = jsonDouble(spec, "properties.padding_bottom", -1.0);

            if(pad_all   >= 0) button->setPadding(pad_all);
            if(pad_x     >= 0) button->setPadding(pad_x, pad_y >= 0 ? pad_y : pad_x);
            if(pad_y     >= 0 && pad_x < 0) button->setPadding(pad_y, pad_y);
            if(pad_left  >= 0) button->setPaddingLeft(pad_left);
            if(pad_right >= 0) button->setPaddingRight(pad_right);
            if(pad_top   >= 0) button->setPaddingTop(pad_top);
            if(pad_bot   >= 0) button->setPaddingBottom(pad_bot);
        }

        ElaraCheckboxWidget* checkbox = dynamic_cast<ElaraCheckboxWidget*>(widget);
        if(checkbox) {
            String text = spec.getStringValue("properties.text");
            int font_size = jsonInt(spec, "properties.font_size", 14);

            if(text.length() > 0) {
                checkbox->setText(text);
            }

            checkbox->setChecked(
                jsonString(spec, "properties.checked", String("false")) == String("true")
            );
            checkbox->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            checkbox->setFontSize((double)font_size);
        }

        ElaraRadioButtonWidget* radio = dynamic_cast<ElaraRadioButtonWidget*>(widget);
        if(radio) {
            String text = spec.getStringValue("properties.text");
            int font_size = jsonInt(spec, "properties.font_size", 14);

            if(text.length() > 0) {
                radio->setText(text);
            }

            radio->setGroup(jsonString(spec, "properties.group", String("default")));
            radio->setChecked(
                jsonString(spec, "properties.checked", String("false")) == String("true")
            );
            radio->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            radio->setFontSize((double)font_size);
        }

        ElaraJsonLabelWidget* label = dynamic_cast<ElaraJsonLabelWidget*>(widget);
        if(label) {
            String text = spec.getStringValue("properties.text");
            int font_size = jsonInt(spec, "properties.font_size", 16);

            if(text.length() > 0) {
                label->setText(text);
            }

            label->setFontSize((double)font_size);
        }

        ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget);
        if(input) {
            String text = spec.getStringValue("properties.text");
            String placeholder = spec.getStringValue("properties.placeholder");
            int font_size = jsonInt(spec, "properties.font_size", 14);

            if(text.length() > 0) {
                input->setText(text);
            }

            if(placeholder.length() > 0) {
                input->setPlaceholder(placeholder);
            }

            input->setFontSize((double)font_size);
        }

        ElaraSliderWidget* slider = dynamic_cast<ElaraSliderWidget*>(widget);
        if(slider) {
            slider->setOrientation(jsonString(spec, "properties.orientation", String("horizontal")));
            slider->setRange(
                jsonDouble(spec, "properties.min", 0.0),
                jsonDouble(spec, "properties.max", 100.0)
            );
            slider->setStep(jsonDouble(spec, "properties.step", 1.0));
            slider->setValue(jsonDouble(spec, "properties.value", 0.0));
        }

        ElaraSpinnerWidget* spinner = dynamic_cast<ElaraSpinnerWidget*>(widget);
        if(spinner) {
            spinner->setRange(
                jsonDouble(spec, "properties.min", 0.0),
                jsonDouble(spec, "properties.max", 100.0)
            );
            spinner->setStep(jsonDouble(spec, "properties.step", 1.0));
            spinner->setValue(jsonDouble(spec, "properties.value", 0.0));
            spinner->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            spinner->setFontSize((double)jsonInt(spec, "properties.font_size", 14));
        }

        ElaraListViewWidget* list = dynamic_cast<ElaraListViewWidget*>(widget);
        if(list) {
            list->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            list->setFontSize((double)jsonInt(spec, "properties.font_size", 14));
        }

        ElaraTreeViewWidget* tree = dynamic_cast<ElaraTreeViewWidget*>(widget);
        if(tree) {
            tree->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            tree->setFontSize((double)jsonInt(spec, "properties.font_size", 14));
        }

        ElaraRichTextEditWidget* rich = dynamic_cast<ElaraRichTextEditWidget*>(widget);
        if(rich) {
            String text = spec.getStringValue("properties.text");
            String placeholder = spec.getStringValue("properties.placeholder");
            int font_size = jsonInt(spec, "properties.font_size", 14);

            if(text.length() > 0) {
                rich->setText(text);
            }

            if(placeholder.length() > 0) {
                rich->setPlaceholder(placeholder);
            }

            rich->setFontSize((double)font_size);
        }

        ElaraCodeEditorWidget* code_editor = dynamic_cast<ElaraCodeEditorWidget*>(widget);
        if(code_editor) {
            String text = spec.getStringValue("properties.text");
            String language = spec.getStringValue("properties.language");
            int font_size = jsonInt(spec, "properties.font_size", 14);

            if(text.length() > 0) {
                code_editor->setText(text);
            }

            code_editor->setLanguage(language.length() > 0 ? language : String("plain"));
            code_editor->setFontSize((double)font_size);
            code_editor->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            code_editor->setReadOnly(
                jsonString(spec, "properties.read_only", String("false")) == String("true")
            );
        }

        ElaraToolBarWidget* toolbar = dynamic_cast<ElaraToolBarWidget*>(widget);
        if(toolbar) {
            String orientation = spec.getStringValue("properties.orientation");
            if(orientation == String("vertical")) {
                toolbar->setOrientation(ELARA_TOOLBAR_VERTICAL);
            } else if(orientation == String("horizontal")) {
                toolbar->setOrientation(ELARA_TOOLBAR_HORIZONTAL);
            }
            toolbar->setFontSize((double)jsonInt(spec, "properties.font_size", 14));

            double pad_x = jsonDouble(spec, "properties.item_padding_x", -1.0);
            double pad_y = jsonDouble(spec, "properties.item_padding_y", -1.0);
            if(pad_x >= 0.0 || pad_y >= 0.0) {
                toolbar->setItemPadding(
                    pad_x >= 0.0 ? pad_x : 10.0,
                    pad_y >= 0.0 ? pad_y : 6.0
                );
            }

            double spacing = jsonDouble(spec, "properties.item_spacing", -1.0);
            if(spacing >= 0.0) {
                toolbar->setItemSpacing(spacing);
            }
        }

        ElaraDensityMapWidget* density = dynamic_cast<ElaraDensityMapWidget*>(widget);
        if(density) {
            unsigned long long base_capacity = (unsigned long long)jsonInt(spec, "properties.base_capacity", 8);
            unsigned long long multiplier = (unsigned long long)jsonInt(spec, "properties.capacity_multiplier", 2);
            int layer_count = jsonInt(spec, "properties.layer_count", 16);

            density->setPowerProfile(base_capacity, multiplier, layer_count);

            String demo_type = spec.getStringValue("demo_data.type");
            if(demo_type == String("modulo_sequence")) {
                unsigned long long sample_count = (unsigned long long)jsonInt(spec, "demo_data.sample_count", 65536);
                unsigned long long sample_multiplier = (unsigned long long)jsonInt(spec, "demo_data.sample_multiplier", 2);
                density->generateModuloSequence(sample_count, sample_multiplier);
            }
        }

        ElaraMultiAxisLineChartWidget* chart = dynamic_cast<ElaraMultiAxisLineChartWidget*>(widget);
        if(chart) {
            String chart_title = spec.getStringValue("properties.title");

            if(chart_title.length() > 0) {
                chart->setTitle(chart_title);
            }

            chart->setShowPoints(jsonString(spec, "properties.show_points", String("true")) == String("true"));
            applyLineChartDemoData(chart, spec);
        }

        ElaraComboBoxWidget* combo = dynamic_cast<ElaraComboBoxWidget*>(widget);
        if(combo) {
            combo->setEnabled(
                jsonString(spec, "properties.enabled", String("true")) != String("false")
            );
            combo->setFontSize((double)jsonInt(spec, "properties.font_size", 13));

            combo->clearItems();
            Array< Ref<JsonValue> > combo_items = spec.getArray("sections.items");
            for(int i = 0; i < (int)combo_items.length(); i++) {
                Json item_json(combo_items[i]->toString());
                combo->addItem(
                    item_json.getStringValue("id"),
                    item_json.getStringValue("label")
                );
            }

            String sel_id = spec.getStringValue("properties.selected_id");
            if(sel_id.length() > 0) {
                combo->setSelectedId(sel_id);
            }
        }

        ElaraOpenClSurfaceWidget* opencl_surface = dynamic_cast<ElaraOpenClSurfaceWidget*>(widget);
        if(opencl_surface) {
            opencl_surface->setBackendId(
                jsonString(spec, "properties.backend", String("opencl"))
            );
            opencl_surface->setKernelName(
                jsonString(spec, "properties.kernel_name", String(""))
            );
            opencl_surface->setOverlayText(
                jsonString(spec, "properties.overlay_text", String(""))
            );
            opencl_surface->setVirtualSize(
                jsonDouble(spec, "properties.virtual_width", 1000.0),
                jsonDouble(spec, "properties.virtual_height", 1000.0)
            );

            opencl_surface->clearCommands();
            Array< Ref<JsonValue> > surface_commands = spec.getArray("commands");
            if(surface_commands.length() <= 0) {
                surface_commands = spec.getArray("sections.commands");
            }
            for(int i = 0; i < (int)surface_commands.length(); i++) {
                Json command_json(surface_commands[i]->toString());
                String op = command_json.getStringValue("op");

                if(op == String("clear")) {
                    opencl_surface->addClear(
                        jsonValueDouble(command_json.getJsonValue("r"), 0.10),
                        jsonValueDouble(command_json.getJsonValue("g"), 0.11),
                        jsonValueDouble(command_json.getJsonValue("b"), 0.14)
                    );
                } else if(op == String("rect")) {
                    opencl_surface->addRect(
                        jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("w"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("h"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                    );
                } else if(op == String("line")) {
                    opencl_surface->addLine(
                        jsonValueDouble(command_json.getJsonValue("x0"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("y0"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("x1"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("y1"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                    );
                } else if(op == String("text")) {
                    opencl_surface->addText(
                        jsonValueDouble(command_json.getJsonValue("x"), 0.0),
                        jsonValueDouble(command_json.getJsonValue("y"), 0.0),
                        command_json.getStringValue("text"),
                        jsonValueDouble(command_json.getJsonValue("size"), 24.0),
                        jsonValueDouble(command_json.getJsonValue("r"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("g"), 1.0),
                        jsonValueDouble(command_json.getJsonValue("b"), 1.0)
                    );
                }
            }
        }

        ElaraMenuBarWidget* menu_bar = dynamic_cast<ElaraMenuBarWidget*>(widget);
        if(menu_bar) {
            menu_bar->setFontSize((double)jsonInt(spec, "properties.font_size", 14));
            menu_bar->setCustomChrome(jsonBool(spec, "properties.custom_chrome", false));
            menu_bar->setWindowTitle(spec.getStringValue("properties.window_title"));
            applyMenuBarButtons(menu_bar, spec);
            applyMenuBarMenus(menu_bar, spec);
        }
    }
};

ElaraJsonUiProtocol::ElaraJsonUiProtocol(
    ElaraRootWidget* root_widget,
    ElaraTheme* ui_theme
) : root(root_widget),
    theme(ui_theme) {
    registerFactory(Ref<ElaraJsonWidgetFactory>(new ElaraBuiltinWidgetFactory()));
}

ElaraJsonUiProtocol::~ElaraJsonUiProtocol() {}

void ElaraJsonUiProtocol::appendFactory(Ref<ElaraJsonWidgetFactory> factory) {
    if(!factory) {
        return;
    }

    factories.push(factory);
}

void ElaraJsonUiProtocol::registerFactory(Ref<ElaraJsonWidgetFactory> factory) {
    appendFactory(factory);
}

bool ElaraJsonUiProtocol::clearChildren(ElaraWidgetHandle target_handle) {
    if(!root) {
        return false;
    }

    Ref<ElaraWidget> widget = root->getWidget(target_handle);

    if(!widget) {
        return false;
    }

    widget->clearChildren();
    return true;
}

bool ElaraJsonUiProtocol::replaceChildren(
    ElaraWidgetHandle target_handle,
    const String& json_text
) {
    if(!root) {
        return false;
    }

    Ref<ElaraWidget> widget = root->getWidget(target_handle);

    if(!widget) {
        return false;
    }

    String spec_text(json_text);
    spec_text = spec_text.trim();

    if(spec_text.length() <= 0) {
        return false;
    }

    Json spec(
        spec_text.startsWith("[")
            ? String("{\"children\":") + spec_text + String("}")
            : spec_text
    );

    widget->clearChildren();
    return replaceChildren(widget, spec);
}

bool ElaraJsonUiProtocol::addTab(ElaraWidgetHandle target_handle, const String& json_text) {
    if(!root) {
        return false;
    }

    Ref<ElaraWidget> widget = root->getWidget(target_handle);

    if(!widget) {
        return false;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(widget.getPtr());

    if(!tabs) {
        return false;
    }

    Json spec(json_text);
    String title = jsonString(spec, "title", String("Tab"));
    String btn_glyph = spec.getStringValue("button_glyph");
    String btn_action = spec.getStringValue("button_action");

    Ref<JsonValue> child_value = spec.getJsonValue("child");

    if(!child_value || child_value->getType() == JsonValue::INVALID) {
        return false;
    }

    Json child_spec(child_value);
    ElaraWidget* child = createWidget(child_spec);

    if(!child) {
        return false;
    }

    tabs->addTab(title, child, btn_glyph, btn_action);
    return true;
}

bool ElaraJsonUiProtocol::removeTab(ElaraWidgetHandle target_handle, int index) {
    if(!root) {
        return false;
    }

    Ref<ElaraWidget> widget = root->getWidget(target_handle);

    if(!widget) {
        return false;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(widget.getPtr());

    if(!tabs) {
        return false;
    }

    tabs->removeTab(index);
    return true;
}

bool ElaraJsonUiProtocol::setActiveTab(ElaraWidgetHandle target_handle, int index) {
    if(!root) {
        return false;
    }

    Ref<ElaraWidget> widget = root->getWidget(target_handle);

    if(!widget) {
        return false;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(widget.getPtr());

    if(!tabs) {
        return false;
    }

    tabs->setActiveTab(index);
    return true;
}

Ref<ElaraJsonWidgetFactory> ElaraJsonUiProtocol::findFactory(const String& type) const {
    for(int i = 0; i < (int)factories.length(); i++) {
        Ref<ElaraJsonWidgetFactory> factory = factories[i];

        if(factory && factory->supports(type)) {
            return factory;
        }
    }

    return Ref<ElaraJsonWidgetFactory>(0);
}

ElaraWidget* ElaraJsonUiProtocol::createWidget(const Json& spec) {
    String id = spec.getStringValue("id");
    String type = spec.getStringValue("type");

    if(id.length() <= 0 || type.length() <= 0) {
        printf("json ui: widget requires id and type\n");
        return 0;
    }

    Ref<ElaraJsonWidgetFactory> factory = findFactory(type);

    if(!factory) {
        printf("json ui: no factory for widget type %s\n", (const char*)type);
        return 0;
    }

    return factory->createWidget(root, id, spec);
}

bool ElaraJsonUiProtocol::replaceChildren(
    Ref<ElaraWidget> target_widget,
    const Json& spec
) {
    if(!target_widget) {
        return false;
    }

    ElaraGridLayout* grid = dynamic_cast<ElaraGridLayout*>(target_widget.getPtr());
    if(grid) {
        Array< Ref<JsonValue> > children = spec.getArray("children");

        for(int i = 0; i < (int)children.length(); i++) {
            Json child_spec(children[i]);
            ElaraWidget* child = createWidget(child_spec);

            if(!child) {
                continue;
            }

            String id = child_spec.getStringValue("id");
            int column = jsonInt(child_spec, "cell.column", 0);
            int row = jsonInt(child_spec, "cell.row", 0);
            int column_span = jsonInt(child_spec, "cell.column_span", 1);
            int row_span = jsonInt(child_spec, "cell.row_span", 1);

            grid->addWidget(id, column, row, column_span, row_span);
            grid->addChild(Ref<ElaraWidget>(child));
        }

        return true;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(target_widget.getPtr());
    if(tabs) {
        Array< Ref<JsonValue> > tab_specs = spec.getArray("tabs");

        for(int i = 0; i < (int)tab_specs.length(); i++) {
            Json tab_spec(tab_specs[i]);
            String title = jsonString(tab_spec, "title", String("Tab"));
            Ref<JsonValue> widget_value = tab_spec.getJsonValue("widget");

            if(!widget_value || widget_value->getType() == JsonValue::INVALID) {
                continue;
            }

            Json widget_spec(widget_value);
            ElaraWidget* child = createWidget(widget_spec);

            if(child) {
                String btn_glyph = tab_spec.getStringValue("button_glyph");
                String btn_action = tab_spec.getStringValue("button_action");
                tabs->addTab(title, child, btn_glyph, btn_action);
            }
        }

        return true;
    }

    ElaraListViewWidget* list = dynamic_cast<ElaraListViewWidget*>(target_widget.getPtr());
    if(list) {
        list->clearItems();
        Array< Ref<JsonValue> > items = spec.getArray("items");
        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            list->addItem(ElaraListViewItem(
                item_json.getStringValue("id"),
                item_json.getStringValue("label")
            ));
        }
        return true;
    }

    ElaraComboBoxWidget* combo_target = dynamic_cast<ElaraComboBoxWidget*>(target_widget.getPtr());
    if(combo_target) {
        combo_target->clearItems();
        Array< Ref<JsonValue> > items = spec.getArray("items");
        for(int i = 0; i < (int)items.length(); i++) {
            Json item_json(items[i]->toString());
            combo_target->addItem(
                item_json.getStringValue("id"),
                item_json.getStringValue("label")
            );
        }
        return true;
    }

    ElaraTreeViewWidget* tree_target = dynamic_cast<ElaraTreeViewWidget*>(target_widget.getPtr());
    if(tree_target) {
        tree_target->clearNodes();
        Array< Ref<JsonValue> > nodes = spec.getArray("nodes");
        for(int i = 0; i < (int)nodes.length(); i++) {
            Json node_json(nodes[i]->toString());
            tree_target->addRootNode(parseTreeNodeSpec(node_json));
        }
        return true;
    }

    Array< Ref<JsonValue> > children = spec.getArray("children");

    for(int i = 0; i < (int)children.length(); i++) {
        Json child_spec(children[i]);
        ElaraWidget* child = createWidget(child_spec);

        if(child) {
            target_widget->addChild(Ref<ElaraWidget>(child));
        }
    }

    return true;
}

void ElaraJsonUiProtocol::applyTheme(const Json& document) {
    if(!theme) {
        return;
    }

    String mode = document.getStringValue("theme.mode");

    if(mode.length() > 0) {
        theme->setMode(mode);
    }

    if(root) {
        root->setPalette(theme->getPalette());
    }
}

void ElaraJsonUiProtocol::applyRoot(const Json& document) {
    if(!root) {
        return;
    }

    String content = document.getStringValue("root.content");
    String popup = document.getStringValue("root.popup");
    Array< Ref<JsonValue> > popup_handles = document.getArray("root.popups");

    if(content.length() > 0) {
        root->setContent(content);
    }

    root->clearPopups();

    if(popup.length() > 0) {
        root->pushPopup(popup);
    }

    for(int i = 0; i < (int)popup_handles.length(); i++) {
        String popup_handle = popup_handles[i]->toString().trim();

        if(popup_handle.length() >= 2 &&
           popup_handle.startsWith("\"") &&
           popup_handle.endsWith("\"")) {
            popup_handle = popup_handle.substr(1, popup_handle.length() - 2);
        }

        if(popup_handle.length() > 0) {
            root->pushPopup(popup_handle);
        }
    }
}

void ElaraJsonUiProtocol::createTopLevelWidgets(const Json& document) {
    Array< Ref<JsonValue> > widgets = document.getArray("widgets");

    for(int i = 0; i < (int)widgets.length(); i++) {
        Json spec(widgets[i]);
        ElaraWidget* widget = createWidget(spec);

        if(!widget) {
            continue;
        }

        root->addChild(Ref<ElaraWidget>(widget));
    }
}

static EvColor jsonToEvColor(const Array< Ref<JsonValue> >& arr) {
    float r = arr.length() > 0 ? (float)jsonValueDouble(arr[0], 0.0) : 0.0f;
    float g = arr.length() > 1 ? (float)jsonValueDouble(arr[1], 0.0) : 0.0f;
    float b = arr.length() > 2 ? (float)jsonValueDouble(arr[2], 0.0) : 0.0f;
    float a = arr.length() > 3 ? (float)jsonValueDouble(arr[3], 1.0) : 1.0f;
    return ev_rgba(
        (unsigned char)(r * 255),
        (unsigned char)(g * 255),
        (unsigned char)(b * 255),
        (unsigned char)(a * 255)
    );
}

static void applyVectorNodeStyle(EvNode *node, const Json& spec) {
    Array< Ref<JsonValue> > fill = spec.getArray("fill");
    Array< Ref<JsonValue> > stroke = spec.getArray("stroke");
    float stroke_width = (float)jsonDouble(spec, "stroke_width", 1.0);

    if (fill.length() >= 3) {
        ev_set_fill(node, jsonToEvColor(fill));
    }

    if (stroke.length() >= 3) {
        ev_set_stroke(node, jsonToEvColor(stroke), stroke_width);
    }
}

static EvNode *parseVectorNode(const Json& spec);

static void parseVectorChildren(EvNode *parent, const Json& spec) {
    Array< Ref<JsonValue> > children = spec.getArray("children");
    for (int i = 0; i < (int)children.length(); i++) {
        Json child_spec(children[i]);
        EvNode *child = parseVectorNode(child_spec);
        if (child) {
            ev_node_add_child(parent, child);
        }
    }
}

static EvNode *parseVectorNode(const Json& spec) {
    String type = spec.getStringValue("type");

    if (type == String("rect")) {
        float x = (float)jsonDouble(spec, "x", 0.0);
        float y = (float)jsonDouble(spec, "y", 0.0);
        float w = (float)jsonDouble(spec, "w", 0.0);
        float h = (float)jsonDouble(spec, "h", 0.0);
        EvNode *node = ev_rect(x, y, w, h);
        if (node) {
            applyVectorNodeStyle(node, spec);
        }
        return node;
    }

    if (type == String("circle")) {
        float x = (float)jsonDouble(spec, "x", 0.0);
        float y = (float)jsonDouble(spec, "y", 0.0);
        float r = (float)jsonDouble(spec, "r", 0.0);
        EvNode *node = ev_circle(x, y, r);
        if (node) {
            applyVectorNodeStyle(node, spec);
        }
        return node;
    }

    if (type == String("line")) {
        float x1 = (float)jsonDouble(spec, "x1", 0.0);
        float y1 = (float)jsonDouble(spec, "y1", 0.0);
        float x2 = (float)jsonDouble(spec, "x2", 0.0);
        float y2 = (float)jsonDouble(spec, "y2", 0.0);
        EvNode *node = ev_line(x1, y1, x2, y2);
        if (node) {
            applyVectorNodeStyle(node, spec);
        }
        return node;
    }

    if (type == String("text")) {
        float x = (float)jsonDouble(spec, "x", 0.0);
        float y = (float)jsonDouble(spec, "y", 0.0);
        float size = (float)jsonDouble(spec, "size", 12.0);
        String text = spec.getStringValue("text");

        EvNode *node = ev_node_create(EV_NODE_TEXT);
        if (node) {
            node->data.text.x = x;
            node->data.text.y = y;
            node->data.text.size = size;
            node->data.text.text = strdup((const char*)text);
            node->data.text.font = 0;
            applyVectorNodeStyle(node, spec);
        }
        return node;
    }

    if (type == String("group")) {
        EvNode *node = ev_group();
        if (node) {
            applyVectorNodeStyle(node, spec);
            parseVectorChildren(node, spec);
        }
        return node;
    }

    printf("json overlay: unknown node type '%s'\n", (const char*)type);
    return 0;
}

void ElaraJsonUiProtocol::applyOverlays(const Json& document) {
    if (!root) {
        return;
    }

    Array< Ref<JsonValue> > overlays = document.getArray("overlays");

    for (int i = 0; i < (int)overlays.length(); i++) {
        Json overlay_spec(overlays[i]);

        String id = overlay_spec.getStringValue("id");
        if (id.length() <= 0) {
            printf("json overlay: missing id, skipping\n");
            continue;
        }

        float x = (float)jsonDouble(overlay_spec, "x", 0.0);
        float y = (float)jsonDouble(overlay_spec, "y", 0.0);
        float w = (float)jsonDouble(overlay_spec, "width", 100.0);
        float h = (float)jsonDouble(overlay_spec, "height", 100.0);

        EvDocument *doc = ev_document_create(w, h);
        if (!doc) {
            continue;
        }

        Array< Ref<JsonValue> > nodes = overlay_spec.getArray("nodes");
        for (int j = 0; j < (int)nodes.length(); j++) {
            Json node_spec(nodes[j]);
            EvNode *node = parseVectorNode(node_spec);
            if (node) {
                ev_document_add_child(doc, node);
            }
        }

        String anchor_x = overlay_spec.getStringValue("anchor_x");
        String anchor_y = overlay_spec.getStringValue("anchor_y");

        ElaraVectorDocument overlay;
        overlay.setDocument(doc);
        overlay.setPosition(x, y);
        overlay.setAnchorH(anchor_x == String("right")
            ? ELARA_OVERLAY_ANCHOR_RIGHT : ELARA_OVERLAY_ANCHOR_LEFT);
        overlay.setAnchorV(anchor_y == String("bottom")
            ? ELARA_OVERLAY_ANCHOR_BOTTOM : ELARA_OVERLAY_ANCHOR_TOP);

        root->addVectorOverlay(id, overlay);
        printf("json overlay: '%s' loaded at (%.0f, %.0f)\n", (const char*)id, x, y);
    }
}

bool ElaraJsonUiProtocol::load(const String& json_text) {
    Json document(json_text);

    applyTheme(document);
    createTopLevelWidgets(document);
    applyRoot(document);
    applyOverlays(document);

    return true;
}

bool ElaraJsonUiProtocol::loadFile(const String& path) {
    File file((const char*)path);
    Memory data = file.getMemory();
    String json_text((const char*)data.getPtr(), data.length());
    return load(json_text);
}

bool ElaraJsonUiProtocol::setThemeMode(const String& mode) {
    if(!theme || mode.length() == 0) {
        return false;
    }

    bool ok = theme->setMode(mode);

    if(ok && root) {
        root->setPalette(theme->getPalette());
    }

    return ok;
}

}

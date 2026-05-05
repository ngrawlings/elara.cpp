#include "ElaraJsonUiProtocol.h"

#include <stdio.h>
#include <libelaraio/File.h>
#include <libelaraformat/json/types/JsonValue.h>

#include <libelaraui/frontend/widgets/ElaraTabWidget.h>
#include <libelaraui/frontend/widgets/ElaraPopupWidget.h>
#include <libelaraui/frontend/layouts/ElaraGridLayout.h>
#include <libelaraui/frontend/widgets/ElaraButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraCheckboxWidget.h>
#include <libelaraui/frontend/widgets/ElaraRadioButtonWidget.h>
#include <libelaraui/frontend/widgets/ElaraRichTextEditWidget.h>
#include <libelaraui/frontend/widgets/ElaraSliderWidget.h>
#include <libelaraui/frontend/widgets/ElaraSpinnerWidget.h>
#include <libelaraui/frontend/widgets/ElaraListViewWidget.h>
#include <libelaraui/frontend/widgets/ElaraTextInputWidget.h>
#include <libelaraui/frontend/widgets/ElaraTreeViewWidget.h>
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

class ElaraJsonLabelWidget : public ElaraWidget {
private:
    String value;
    double font_size;

public:
    ElaraJsonLabelWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
        : ElaraWidget(root, handle),
          value("Label"),
          font_size(16) {}

    void setText(const String& label_text) {
        value = label_text;
    }

    void setFontSize(double size) {
        font_size = size;
    }

    void draw(ElaraDrawContext* ctx) {
        ElaraPaletteTriplet c = colors("panel", "default");
        ctx->setColor(c.base.r, c.base.g, c.base.b);
        ctx->fillRect(0, 0, width, height);
        ctx->setColor(c.text.r, c.text.g, c.text.b);
        ctx->drawText(12, (height / 2) + (font_size / 2) - 2, value, font_size);
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
            String id = item.getStringValue("id");
            String label = item.getStringValue("label");

            if(id.length() > 0 && label.length() > 0) {
                popup->addItem(id, label);
            }
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

            tabs->addTab(title, child);
        }
    }

    void applyGridTracks(ElaraGridLayout* grid, const Json& spec) {
        Array< Ref<JsonValue> > columns = spec.getArray("columns");

        for(int i = 0; i < (int)columns.length(); i++) {
            Json column(columns[i]);
            String mode = column.getStringValue("mode");

            if(mode == String("fill")) {
                grid->addFillColumn();
            } else {
                grid->addColumn((double)jsonInt(column, "size", 0));
            }
        }

        Array< Ref<JsonValue> > rows = spec.getArray("rows");

        for(int i = 0; i < (int)rows.length(); i++) {
            Json row(rows[i]);
            String mode = row.getStringValue("mode");

            if(mode == String("fill")) {
                grid->addFillRow();
            } else {
                grid->addRow((double)jsonInt(row, "size", 0));
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
            type == String("elara.layouts.grid") ||
            type == String("elara.widgets.button") ||
            type == String("elara.widgets.checkbox") ||
            type == String("elara.widgets.radio_button") ||
            type == String("elara.widgets.label") ||
            type == String("elara.widgets.rich_text_edit") ||
            type == String("elara.widgets.slider") ||
            type == String("elara.widgets.spinner") ||
            type == String("elara.widgets.text_input") ||
            type == String("elara.widgets.list_view") ||
            type == String("elara.widgets.tree_view") ||
            type == String("elara.widgets.surface_panel") ||
            type == String("elara.widgets.density_map") ||
            type == String("elara.widgets.multi_axis_line_chart") ||
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
        } else if(type == String("elara.widgets.label") || type == String("demo.widgets.label")) {
            widget = new ElaraJsonLabelWidget(root, id);
        } else if(type == String("elara.widgets.text_input") || type == String("demo.widgets.text_input")) {
            widget = new ElaraTextInputWidget(root, id);
        } else if(type == String("elara.widgets.surface_panel") || type == String("demo.widgets.surface_panel")) {
            widget = new ElaraJsonSurfacePanelWidget(root, id);
        } else if(type == String("elara.widgets.density_map")) {
            widget = new ElaraDensityMapWidget(root, id);
            // TODO: apply styling attribgutes within the json spec
        } else if(type == String("elara.widgets.multi_axis_line_chart")) {
            widget = new ElaraMultiAxisLineChartWidget(root, id);
        }

        applyProperties(widget, spec);
        return widget;
    }

    void applyProperties(ElaraWidget* widget, const Json& spec) {
        if(!widget) {
            return;
        }

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
                tabs->addTab(title, child);
            }
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

bool ElaraJsonUiProtocol::load(const String& json_text) {
    Json document(json_text);

    applyTheme(document);
    createTopLevelWidgets(document);
    applyRoot(document);

    return true;
}

bool ElaraJsonUiProtocol::loadFile(const String& path) {
    File file((const char*)path);
    Memory data = file.getMemory();
    String json_text((const char*)data.getPtr(), data.length());
    return load(json_text);
}

}

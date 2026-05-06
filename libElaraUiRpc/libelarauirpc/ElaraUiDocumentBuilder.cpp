#include "ElaraUiDocumentBuilder.h"

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonValue.h>
#include <libelaraformat/json/types/JsonString.h>

namespace elara {
namespace ui {
namespace rpc {

namespace {

static String jsonStringLiteral(const String& value) {
    return JsonString(value, true).toString();
}

static String jsonNumberLiteral(double value) {
    return String(value);
}

}

ElaraUiDocumentBuilder::ElaraUiDocumentBuilder()
    : window_title("Elara UI"),
      window_width(800),
      window_height(600),
      window_backend_id("org.elara.ui.app"),
      theme_mode("light") {
}

ElaraUiDocumentBuilder::~ElaraUiDocumentBuilder() {}

void ElaraUiDocumentBuilder::clear() {
    window_title = "Elara UI";
    window_width = 800;
    window_height = 600;
    window_backend_id = "org.elara.ui.app";
    theme_mode = "light";
    root_content = String();
    root_popups.clear();
    widgets.clear();
}

int ElaraUiDocumentBuilder::findWidgetIndex(const String& id) const {
    for(int i = 0; i < (int)widgets.length(); i++) {
        if(widgets[i].id == id) {
            return i;
        }
    }

    return -1;
}

ElaraUiDocumentBuilder::WidgetSpec* ElaraUiDocumentBuilder::getWidgetSpec(const String& id) {
    int index = findWidgetIndex(id);

    if(index < 0) {
        return 0;
    }

    return &widgets[index];
}

const ElaraUiDocumentBuilder::WidgetSpec* ElaraUiDocumentBuilder::getWidgetSpec(const String& id) const {
    int index = findWidgetIndex(id);

    if(index < 0) {
        return 0;
    }

    return &widgets[index];
}

void ElaraUiDocumentBuilder::setField(
    Array<JsonField>* fields,
    const String& name,
    const String& json_value
) {
    if(!fields || name.length() <= 0) {
        return;
    }

    for(int i = 0; i < (int)fields->length(); i++) {
        if((*fields)[i].name == name) {
            (*fields)[i].json_value = json_value;
            return;
        }
    }

    JsonField field;
    field.name = name;
    field.json_value = json_value;
    fields->push(field);
}

void ElaraUiDocumentBuilder::detachChildReference(const String& child_id) {
    for(int i = 0; i < (int)widgets.length(); i++) {
        WidgetSpec* widget = &widgets[i];

        for(int j = (int)widget->children.length() - 1; j >= 0; j--) {
            if(widget->children[j].child_id == child_id) {
                widget->children.remove(j);
            }
        }

        for(int j = (int)widget->grid_children.length() - 1; j >= 0; j--) {
            if(widget->grid_children[j].child_id == child_id) {
                widget->grid_children.remove(j);
            }
        }

        for(int j = (int)widget->tabs.length() - 1; j >= 0; j--) {
            if(widget->tabs[j].child_id == child_id) {
                widget->tabs.remove(j);
            }
        }
    }
}

bool ElaraUiDocumentBuilder::isNestedWidget(const String& id) const {
    for(int i = 0; i < (int)widgets.length(); i++) {
        const WidgetSpec* widget = &widgets[i];

        for(int j = 0; j < (int)widget->children.length(); j++) {
            if(widget->children[j].child_id == id) {
                return true;
            }
        }

        for(int j = 0; j < (int)widget->grid_children.length(); j++) {
            if(widget->grid_children[j].child_id == id) {
                return true;
            }
        }

        for(int j = 0; j < (int)widget->tabs.length(); j++) {
            if(widget->tabs[j].child_id == id) {
                return true;
            }
        }
    }

    return false;
}

String ElaraUiDocumentBuilder::serializeFields(const Array<JsonField>& fields) const {
    String json("{");

    for(int i = 0; i < (int)fields.length(); i++) {
        if(i > 0) {
            json += ",";
        }

        json += jsonStringLiteral(fields[i].name);
        json += ":";
        json += fields[i].json_value;
    }

    json += "}";
    return json;
}

String ElaraUiDocumentBuilder::serializeWidget(const WidgetSpec& spec) const {
    String json("{");
    json += "\"id\":";
    json += jsonStringLiteral(spec.id);
    json += ",\"type\":";
    json += jsonStringLiteral(spec.type);

    if(spec.properties.length() > 0) {
        json += ",\"properties\":";
        json += serializeFields(spec.properties);
    }

    for(int i = 0; i < (int)spec.sections.length(); i++) {
        json += ",";
        json += jsonStringLiteral(spec.sections[i].name);
        json += ":";
        json += spec.sections[i].json_value;
    }

    if(spec.popup_items.length() > 0) {
        json += ",\"items\":[";

        for(int i = 0; i < (int)spec.popup_items.length(); i++) {
            if(i > 0) {
                json += ",";
            }

            json += "{\"id\":";
            json += jsonStringLiteral(spec.popup_items[i].id);
            json += ",\"label\":";
            json += jsonStringLiteral(spec.popup_items[i].label);
            json += "}";
        }

        json += "]";
    }

    if(spec.grid_columns.length() > 0) {
        json += ",\"columns\":[";

        for(int i = 0; i < (int)spec.grid_columns.length(); i++) {
            if(i > 0) {
                json += ",";
            }

            if(spec.grid_columns[i].fill) {
                json += "{\"mode\":\"fill\"";
                if(spec.grid_columns[i].weight > 0 && spec.grid_columns[i].weight != 1.0) {
                    json += ",\"weight\":";
                    json += jsonNumberLiteral(spec.grid_columns[i].weight);
                }
                if(spec.grid_columns[i].resizable_after) {
                    json += ",\"resizable\":true";
                }
                json += "}";
            } else {
                json += "{\"mode\":\"exact\",\"size\":";
                json += jsonNumberLiteral(spec.grid_columns[i].size);
                if(spec.grid_columns[i].resizable_after) {
                    json += ",\"resizable\":true";
                }
                json += "}";
            }
        }

        json += "]";
    }

    if(spec.grid_rows.length() > 0) {
        json += ",\"rows\":[";

        for(int i = 0; i < (int)spec.grid_rows.length(); i++) {
            if(i > 0) {
                json += ",";
            }

            if(spec.grid_rows[i].fill) {
                json += "{\"mode\":\"fill\"";
                if(spec.grid_rows[i].weight > 0 && spec.grid_rows[i].weight != 1.0) {
                    json += ",\"weight\":";
                    json += jsonNumberLiteral(spec.grid_rows[i].weight);
                }
                if(spec.grid_rows[i].resizable_after) {
                    json += ",\"resizable\":true";
                }
                json += "}";
            } else {
                json += "{\"mode\":\"exact\",\"size\":";
                json += jsonNumberLiteral(spec.grid_rows[i].size);
                if(spec.grid_rows[i].resizable_after) {
                    json += ",\"resizable\":true";
                }
                json += "}";
            }
        }

        json += "]";
    }

    if(spec.tabs.length() > 0) {
        json += ",\"tabs\":[";
        bool first = true;

        for(int i = 0; i < (int)spec.tabs.length(); i++) {
            const WidgetSpec* child = getWidgetSpec(spec.tabs[i].child_id);

            if(!child) {
                continue;
            }

            if(!first) {
                json += ",";
            }
            first = false;

            json += "{\"title\":";
            json += jsonStringLiteral(spec.tabs[i].title);
            json += ",\"widget\":";
            json += serializeWidget(*child);
            json += "}";
        }

        json += "]";
    } else if(spec.grid_children.length() > 0) {
        json += ",\"children\":[";
        bool first = true;

        for(int i = 0; i < (int)spec.grid_children.length(); i++) {
            const WidgetSpec* child = getWidgetSpec(spec.grid_children[i].child_id);

            if(!child) {
                continue;
            }

            if(!first) {
                json += ",";
            }
            first = false;

            String child_json = serializeWidget(*child);

            if(child_json.endsWith("}")) {
                child_json = child_json.substr(0, child_json.length() - 1);
            }

            json += child_json;
            json += ",\"cell\":{";
            json += "\"column\":";
            json += String(spec.grid_children[i].column);
            json += ",\"row\":";
            json += String(spec.grid_children[i].row);
            json += ",\"column_span\":";
            json += String(spec.grid_children[i].column_span);
            json += ",\"row_span\":";
            json += String(spec.grid_children[i].row_span);
            json += "}}";
        }

        json += "]";
    } else if(spec.children.length() > 0) {
        json += ",\"children\":[";
        bool first = true;

        for(int i = 0; i < (int)spec.children.length(); i++) {
            const WidgetSpec* child = getWidgetSpec(spec.children[i].child_id);

            if(!child) {
                continue;
            }

            if(!first) {
                json += ",";
            }
            first = false;

            json += serializeWidget(*child);
        }

        json += "]";
    }

    json += "}";
    return json;
}

void ElaraUiDocumentBuilder::createWindow(
    const String& title,
    int width,
    int height,
    const String& backend_id
) {
    window_title = title;
    window_width = width;
    window_height = height;
    window_backend_id = backend_id;
}

void ElaraUiDocumentBuilder::setThemeMode(const String& mode) {
    if(mode.length() > 0) {
        theme_mode = mode;
    }
}

void ElaraUiDocumentBuilder::setRootContent(const String& widget_id) {
    root_content = widget_id;
}

void ElaraUiDocumentBuilder::clearRootPopups() {
    root_popups.clear();
}

void ElaraUiDocumentBuilder::pushRootPopup(const String& widget_id) {
    if(widget_id.length() <= 0) {
        return;
    }

    root_popups.push(widget_id);
}

bool ElaraUiDocumentBuilder::hasWidget(const String& id) const {
    return findWidgetIndex(id) >= 0;
}

bool ElaraUiDocumentBuilder::createWidget(const String& id, const String& type) {
    if(id.length() <= 0 || type.length() <= 0 || hasWidget(id)) {
        return false;
    }

    WidgetSpec spec;
    spec.id = id;
    spec.type = type;
    widgets.push(spec);
    return true;
}

bool ElaraUiDocumentBuilder::createTabs(const String& id) {
    return createWidget(id, "elara.widgets.tabs");
}

bool ElaraUiDocumentBuilder::createPopup(const String& id) {
    return createWidget(id, "elara.widgets.popup");
}

bool ElaraUiDocumentBuilder::createMenuBar(const String& id) {
    return createWidget(id, "elara.widgets.menu_bar");
}

bool ElaraUiDocumentBuilder::createGrid(const String& id) {
    return createWidget(id, "elara.layouts.grid");
}

bool ElaraUiDocumentBuilder::createButton(const String& id, const String& text, const String& action) {
    if(!createWidget(id, "elara.widgets.button")) {
        return false;
    }

    setPropertyString(id, "text", text);
    setPropertyString(id, "action", action);
    return true;
}

bool ElaraUiDocumentBuilder::createCheckbox(const String& id, const String& text, bool checked) {
    if(!createWidget(id, "elara.widgets.checkbox")) {
        return false;
    }

    setPropertyString(id, "text", text);
    setPropertyBool(id, "checked", checked);
    return true;
}

bool ElaraUiDocumentBuilder::createRadioButton(
    const String& id,
    const String& text,
    const String& group,
    bool checked
) {
    if(!createWidget(id, "elara.widgets.radio_button")) {
        return false;
    }

    setPropertyString(id, "text", text);
    setPropertyString(id, "group", group);
    setPropertyBool(id, "checked", checked);
    return true;
}

bool ElaraUiDocumentBuilder::createLabel(const String& id, const String& text, double font_size) {
    if(!createWidget(id, "elara.widgets.label")) {
        return false;
    }

    setPropertyString(id, "text", text);
    setPropertyNumber(id, "font_size", font_size);
    return true;
}

bool ElaraUiDocumentBuilder::createTextInput(const String& id, const String& placeholder, const String& text) {
    if(!createWidget(id, "elara.widgets.text_input")) {
        return false;
    }

    if(placeholder.length() > 0) {
        setPropertyString(id, "placeholder", placeholder);
    }

    if(text.length() > 0) {
        setPropertyString(id, "text", text);
    }

    return true;
}

bool ElaraUiDocumentBuilder::createSlider(
    const String& id,
    const String& orientation,
    double min_value,
    double max_value,
    double value,
    double step
) {
    if(!createWidget(id, "elara.widgets.slider")) {
        return false;
    }

    setPropertyString(id, "orientation", orientation);
    setPropertyNumber(id, "min", min_value);
    setPropertyNumber(id, "max", max_value);
    setPropertyNumber(id, "value", value);
    setPropertyNumber(id, "step", step);
    return true;
}

bool ElaraUiDocumentBuilder::createSpinner(
    const String& id,
    double min_value,
    double max_value,
    double value,
    double step
) {
    if(!createWidget(id, "elara.widgets.spinner")) {
        return false;
    }

    setPropertyNumber(id, "min", min_value);
    setPropertyNumber(id, "max", max_value);
    setPropertyNumber(id, "value", value);
    setPropertyNumber(id, "step", step);
    return true;
}

bool ElaraUiDocumentBuilder::createListView(const String& id) {
    return createWidget(id, "elara.widgets.list_view");
}

bool ElaraUiDocumentBuilder::createTreeView(const String& id) {
    return createWidget(id, "elara.widgets.tree_view");
}

bool ElaraUiDocumentBuilder::createRichTextEdit(const String& id, const String& text) {
    if(!createWidget(id, "elara.widgets.rich_text_edit")) {
        return false;
    }

    setPropertyString(id, "text", text);
    return true;
}

bool ElaraUiDocumentBuilder::createSurfacePanel(const String& id) {
    return createWidget(id, "elara.widgets.surface_panel");
}

bool ElaraUiDocumentBuilder::createDensityMap(const String& id) {
    return createWidget(id, "elara.widgets.density_map");
}

bool ElaraUiDocumentBuilder::createMultiAxisLineChart(const String& id) {
    return createWidget(id, "elara.widgets.multi_axis_line_chart");
}

bool ElaraUiDocumentBuilder::addChild(const String& parent_id, const String& child_id) {
    WidgetSpec* parent = getWidgetSpec(parent_id);

    if(!parent || !hasWidget(child_id)) {
        return false;
    }

    detachChildReference(child_id);

    ChildRef ref;
    ref.child_id = child_id;
    parent->children.push(ref);
    return true;
}

bool ElaraUiDocumentBuilder::addTab(
    const String& tabs_id,
    const String& title,
    const String& child_id
) {
    WidgetSpec* tabs = getWidgetSpec(tabs_id);

    if(!tabs || !hasWidget(child_id)) {
        return false;
    }

    detachChildReference(child_id);

    TabRef ref;
    ref.title = title;
    ref.child_id = child_id;
    tabs->tabs.push(ref);
    return true;
}

bool ElaraUiDocumentBuilder::addPopupItem(
    const String& popup_id,
    const String& item_id,
    const String& label
) {
    WidgetSpec* popup = getWidgetSpec(popup_id);

    if(!popup) {
        return false;
    }

    PopupItem item;
    item.id = item_id;
    item.label = label;
    popup->popup_items.push(item);
    return true;
}

bool ElaraUiDocumentBuilder::addMenuBarMenu(
    const String& menu_bar_id,
    const String& menu_id,
    const String& label
) {
    WidgetSpec* menu_bar = getWidgetSpec(menu_bar_id);

    if(!menu_bar || menu_id.length() <= 0 || label.length() <= 0) {
        return false;
    }

    String menus_json = String("[");
    Ref<JsonValue> existing_value;

    for(int i = 0; i < (int)menu_bar->sections.length(); i++) {
        if(menu_bar->sections[i].name == String("menus")) {
            menus_json = menu_bar->sections[i].json_value;
            break;
        }
    }

    Json menus_document(
        menus_json.length() > 1
            ? String("{\"menus\":") + menus_json + String("}")
            : String("{\"menus\":[]}")
    );
    Array< Ref<JsonValue> > menus = menus_document.getArray("menus");
    String updated("[");
    bool first = true;
    bool found = false;

    for(int i = 0; i < (int)menus.length(); i++) {
        Json menu_json(menus[i]);
        String id = menu_json.getStringValue("id");

        if(!first) {
            updated += ",";
        }
        first = false;

        if(id == menu_id) {
            found = true;
            updated += String("{\"id\":") + jsonStringLiteral(menu_id) +
                       String(",\"label\":") + jsonStringLiteral(label) +
                       String(",\"items\":") + menu_json.getJsonValue("items")->toString() +
                       String("}");
        } else {
            updated += menus[i]->toString();
        }
    }

    if(!found) {
        if(!first) {
            updated += ",";
        }
        updated += String("{\"id\":") + jsonStringLiteral(menu_id) +
                   String(",\"label\":") + jsonStringLiteral(label) +
                   String(",\"items\":[]}");
    }

    updated += "]";
    setField(&menu_bar->sections, "menus", updated);
    return true;
}

bool ElaraUiDocumentBuilder::addMenuBarItem(
    const String& menu_bar_id,
    const String& menu_id,
    const String& item_id,
    const String& label,
    bool enabled,
    const String& shortcut
) {
    WidgetSpec* menu_bar = getWidgetSpec(menu_bar_id);

    if(!menu_bar || menu_id.length() <= 0 || item_id.length() <= 0 || label.length() <= 0) {
        return false;
    }

    String menus_json = String("[]");
    for(int i = 0; i < (int)menu_bar->sections.length(); i++) {
        if(menu_bar->sections[i].name == String("menus")) {
            menus_json = menu_bar->sections[i].json_value;
            break;
        }
    }

    Json menus_document(String("{\"menus\":") + menus_json + String("}"));
    Array< Ref<JsonValue> > menus = menus_document.getArray("menus");
    String updated("[");
    bool first_menu = true;
    bool found_menu = false;

    for(int i = 0; i < (int)menus.length(); i++) {
        Json menu_json(menus[i]);
        String current_id = menu_json.getStringValue("id");
        String current_label = menu_json.getStringValue("label");
        Array< Ref<JsonValue> > items = menu_json.getArray("items");

        if(!first_menu) {
            updated += ",";
        }
        first_menu = false;

        updated += String("{\"id\":") + jsonStringLiteral(current_id) +
                   String(",\"label\":") + jsonStringLiteral(current_label) +
                   String(",\"items\":[");

        bool first_item = true;
        bool inserted = false;

        if(current_id == menu_id) {
            found_menu = true;
        }

        for(int j = 0; j < (int)items.length(); j++) {
            Json item_json(items[j]);
            String current_item_id = item_json.getStringValue("id");

            if(!first_item) {
                updated += ",";
            }
            first_item = false;

            if(current_id == menu_id && current_item_id == item_id) {
                inserted = true;
                updated += String("{\"id\":") + jsonStringLiteral(item_id) +
                           String(",\"label\":") + jsonStringLiteral(label) +
                           String(",\"enabled\":") + (enabled ? String("true") : String("false"));
                if(shortcut.length() > 0) {
                    updated += String(",\"shortcut\":") + jsonStringLiteral(shortcut);
                }
                updated +=
                           String("}");
            } else {
                updated += items[j]->toString();
            }
        }

        if(current_id == menu_id && !inserted) {
            if(!first_item) {
                updated += ",";
            }

            updated += String("{\"id\":") + jsonStringLiteral(item_id) +
                       String(",\"label\":") + jsonStringLiteral(label) +
                       String(",\"enabled\":") + (enabled ? String("true") : String("false"));
            if(shortcut.length() > 0) {
                updated += String(",\"shortcut\":") + jsonStringLiteral(shortcut);
            }
            updated +=
                       String("}");
        }

        updated += "]}";
    }

    if(!found_menu) {
        if(!first_menu) {
            updated += ",";
        }
        updated += String("{\"id\":") + jsonStringLiteral(menu_id) +
                   String(",\"label\":") + jsonStringLiteral(menu_id) +
                   String(",\"items\":[") +
                   String("{\"id\":") + jsonStringLiteral(item_id) +
                   String(",\"label\":") + jsonStringLiteral(label) +
                   String(",\"enabled\":") + (enabled ? String("true") : String("false"));
        if(shortcut.length() > 0) {
            updated += String(",\"shortcut\":") + jsonStringLiteral(shortcut);
        }
        updated +=
                   String("}") +
                   String("]}");
    }

    updated += "]";
    setField(&menu_bar->sections, "menus", updated);
    return true;
}

bool ElaraUiDocumentBuilder::addMenuBarSeparator(const String& menu_bar_id, const String& menu_id) {
    WidgetSpec* menu_bar = getWidgetSpec(menu_bar_id);

    if(!menu_bar || menu_id.length() <= 0) {
        return false;
    }

    String menus_json = String("[]");
    for(int i = 0; i < (int)menu_bar->sections.length(); i++) {
        if(menu_bar->sections[i].name == String("menus")) {
            menus_json = menu_bar->sections[i].json_value;
            break;
        }
    }

    Json menus_document(String("{\"menus\":") + menus_json + String("}"));
    Array< Ref<JsonValue> > menus = menus_document.getArray("menus");
    String updated("[");
    bool first_menu = true;
    bool found_menu = false;

    for(int i = 0; i < (int)menus.length(); i++) {
        Json menu_json(menus[i]);
        String current_id = menu_json.getStringValue("id");
        String current_label = menu_json.getStringValue("label");
        Array< Ref<JsonValue> > items = menu_json.getArray("items");

        if(!first_menu) {
            updated += ",";
        }
        first_menu = false;

        updated += String("{\"id\":") + jsonStringLiteral(current_id) +
                   String(",\"label\":") + jsonStringLiteral(current_label) +
                   String(",\"items\":[");

        bool first_item = true;

        for(int j = 0; j < (int)items.length(); j++) {
            if(!first_item) {
                updated += ",";
            }
            first_item = false;
            updated += items[j]->toString();
        }

        if(current_id == menu_id) {
            found_menu = true;
            if(!first_item) {
                updated += ",";
            }
            updated += String("{\"separator\":true}");
        }

        updated += "]}";
    }

    if(!found_menu) {
        if(!first_menu) {
            updated += ",";
        }
        updated += String("{\"id\":") + jsonStringLiteral(menu_id) +
                   String(",\"label\":") + jsonStringLiteral(menu_id) +
                   String(",\"items\":[{\"separator\":true}]}");
    }

    updated += "]";
    setField(&menu_bar->sections, "menus", updated);
    return true;
}

bool ElaraUiDocumentBuilder::addGridColumnExact(const String& grid_id, double size) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = false;
    track.size = size;
    track.weight = 1.0;
    track.resizable_after = false;
    grid->grid_columns.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::addGridColumnFill(const String& grid_id) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = true;
    track.size = 0;
    track.weight = 1.0;
    track.resizable_after = false;
    grid->grid_columns.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::addGridColumnWeightedFill(const String& grid_id, double weight) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = true;
    track.size = 0;
    track.weight = weight > 0 ? weight : 1.0;
    track.resizable_after = false;
    grid->grid_columns.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::setGridColumnBorderResizable(const String& grid_id, int index, bool enabled) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid || index < 0 || index >= (int)grid->grid_columns.length()) {
        return false;
    }

    grid->grid_columns[index].resizable_after = enabled;
    return true;
}

bool ElaraUiDocumentBuilder::addGridRowExact(const String& grid_id, double size) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = false;
    track.size = size;
    track.weight = 1.0;
    track.resizable_after = false;
    grid->grid_rows.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::addGridRowFill(const String& grid_id) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = true;
    track.size = 0;
    track.weight = 1.0;
    track.resizable_after = false;
    grid->grid_rows.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::addGridRowWeightedFill(const String& grid_id, double weight) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid) {
        return false;
    }

    GridTrack track;
    track.fill = true;
    track.size = 0;
    track.weight = weight > 0 ? weight : 1.0;
    track.resizable_after = false;
    grid->grid_rows.push(track);
    return true;
}

bool ElaraUiDocumentBuilder::setGridRowBorderResizable(const String& grid_id, int index, bool enabled) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid || index < 0 || index >= (int)grid->grid_rows.length()) {
        return false;
    }

    grid->grid_rows[index].resizable_after = enabled;
    return true;
}

bool ElaraUiDocumentBuilder::placeGridChild(
    const String& grid_id,
    const String& child_id,
    int column,
    int row,
    int column_span,
    int row_span
) {
    WidgetSpec* grid = getWidgetSpec(grid_id);

    if(!grid || !hasWidget(child_id)) {
        return false;
    }

    detachChildReference(child_id);

    GridPlacement placement;
    placement.child_id = child_id;
    placement.column = column;
    placement.row = row;
    placement.column_span = column_span;
    placement.row_span = row_span;
    grid->grid_children.push(placement);
    return true;
}

bool ElaraUiDocumentBuilder::setPropertyString(
    const String& widget_id,
    const String& name,
    const String& value
) {
    WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget) {
        return false;
    }

    setField(&widget->properties, name, jsonStringLiteral(value));
    return true;
}

bool ElaraUiDocumentBuilder::setPropertyNumber(
    const String& widget_id,
    const String& name,
    double value
) {
    WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget) {
        return false;
    }

    setField(&widget->properties, name, jsonNumberLiteral(value));
    return true;
}

bool ElaraUiDocumentBuilder::setPropertyBool(
    const String& widget_id,
    const String& name,
    bool value
) {
    WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget) {
        return false;
    }

    setField(&widget->properties, name, value ? String("true") : String("false"));
    return true;
}

bool ElaraUiDocumentBuilder::setPropertyJson(
    const String& widget_id,
    const String& name,
    const String& raw_json
) {
    WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget || raw_json.length() <= 0) {
        return false;
    }

    setField(&widget->properties, name, raw_json);
    return true;
}

bool ElaraUiDocumentBuilder::setSectionJson(
    const String& widget_id,
    const String& section_name,
    const String& raw_json
) {
    WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget || raw_json.length() <= 0) {
        return false;
    }

    setField(&widget->sections, section_name, raw_json);
    return true;
}

String ElaraUiDocumentBuilder::toJson() const {
    String json("{");
    json += "\"elara_ui_protocol\":1";
    json += ",\"window\":{";
    json += "\"title\":";
    json += jsonStringLiteral(window_title);
    json += ",\"width\":";
    json += String(window_width);
    json += ",\"height\":";
    json += String(window_height);
    json += ",\"backend_id\":";
    json += jsonStringLiteral(window_backend_id);
    json += "}";
    json += ",\"theme\":{\"mode\":";
    json += jsonStringLiteral(theme_mode);
    json += "}";
    json += ",\"root\":{";
    json += "\"content\":";
    json += jsonStringLiteral(root_content);

    if(root_popups.length() > 0) {
        json += ",\"popup\":";
        json += jsonStringLiteral(root_popups[0]);
        json += ",\"popups\":[";

        for(int i = 0; i < (int)root_popups.length(); i++) {
            if(i > 0) {
                json += ",";
            }

            json += jsonStringLiteral(root_popups[i]);
        }

        json += "]";
    }

    json += "}";
    json += ",\"widgets\":[";

    bool first = true;

    for(int i = 0; i < (int)widgets.length(); i++) {
        if(isNestedWidget(widgets[i].id)) {
            continue;
        }

        if(!first) {
            json += ",";
        }

        first = false;
        json += serializeWidget(widgets[i]);
    }

    json += "]}";
    return json;
}

String ElaraUiDocumentBuilder::widgetJson(const String& widget_id) const {
    const WidgetSpec* widget = getWidgetSpec(widget_id);

    if(!widget) {
        return String();
    }

    return serializeWidget(*widget);
}

}
}
}

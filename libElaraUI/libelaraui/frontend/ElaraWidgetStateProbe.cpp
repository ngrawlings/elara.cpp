#include "ElaraWidgetStateProbe.h"

#include "layouts/ElaraGridLayout.h"
#include "widgets/ElaraButtonWidget.h"
#include "widgets/ElaraCheckboxWidget.h"
#include "widgets/ElaraLabelWidget.h"
#include "widgets/ElaraMenuBarWidget.h"
#include "widgets/ElaraPopupWidget.h"
#include "widgets/ElaraRadioButtonWidget.h"
#include "widgets/ElaraRootWidget.h"
#include "widgets/ElaraRichTextEditWidget.h"
#include "widgets/ElaraSliderWidget.h"
#include "widgets/ElaraSpinnerWidget.h"
#include "widgets/ElaraTabWidget.h"
#include "widgets/ElaraTextInputWidget.h"
#include "widgets/ElaraComboBoxWidget.h"
#include "widgets/ElaraListViewWidget.h"
#include "widgets/ElaraTreeViewWidget.h"
#include "widgets/instruments/ElaraMultiAxisLineChartWidget.h"
#include <libelaraformat/json/Json.h>

namespace elara {

namespace {

String jsonBoolean(bool value) {
    return value ? String("true") : String("false");
}

String jsonStringField(
    const String& name,
    const String& value,
    bool include_prefix
) {
    String json = include_prefix ? String(",") : String("");
    json += String("\"") + name + String("\":\"") + JsonString::encode(value) + String("\"");
    return json;
}

String jsonBoolField(
    const String& name,
    bool value,
    bool include_prefix
) {
    String json = include_prefix ? String(",") : String("");
    json += String("\"") + name + String("\":") + jsonBoolean(value);
    return json;
}

String jsonDoubleField(
    const String& name,
    double value,
    bool include_prefix
) {
    String json = include_prefix ? String(",") : String("");
    json += String("\"") + name + String("\":") + String(value);
    return json;
}

String jsonIntField(
    const String& name,
    int value,
    bool include_prefix
) {
    String json = include_prefix ? String(",") : String("");
    json += String("\"") + name + String("\":") + String(value);
    return json;
}

}

String ElaraWidgetStateProbe::widgetHandleToString(ElaraWidgetHandle handle) {
    Memory memory = handle.getHandle();
    return String((const char*)memory.getPtr(), memory.length());
}

String ElaraWidgetStateProbe::widgetTypeName(ElaraWidget* widget) {
    if(dynamic_cast<ElaraRootWidget*>(widget)) {
        return "ElaraRootWidget";
    }

    if(dynamic_cast<ElaraTabWidget*>(widget)) {
        return "ElaraTabWidget";
    }

    if(dynamic_cast<ElaraMenuBarWidget*>(widget)) {
        return "ElaraMenuBarWidget";
    }

    if(dynamic_cast<ElaraPopupWidget*>(widget)) {
        return "ElaraPopupWidget";
    }

    if(dynamic_cast<ElaraGridLayout*>(widget)) {
        return "ElaraGridLayout";
    }

    if(dynamic_cast<ElaraButtonWidget*>(widget)) {
        return "ElaraButtonWidget";
    }

    if(dynamic_cast<ElaraCheckboxWidget*>(widget)) {
        return "ElaraCheckboxWidget";
    }

    if(dynamic_cast<ElaraRadioButtonWidget*>(widget)) {
        return "ElaraRadioButtonWidget";
    }

    if(dynamic_cast<ElaraLabelWidget*>(widget)) {
        return "ElaraLabelWidget";
    }

    if(dynamic_cast<ElaraTextInputWidget*>(widget)) {
        return "ElaraTextInputWidget";
    }

    if(dynamic_cast<ElaraSliderWidget*>(widget)) {
        return "ElaraSliderWidget";
    }

    if(dynamic_cast<ElaraSpinnerWidget*>(widget)) {
        return "ElaraSpinnerWidget";
    }

    if(dynamic_cast<ElaraComboBoxWidget*>(widget)) {
        return "ElaraComboBoxWidget";
    }

    if(dynamic_cast<ElaraListViewWidget*>(widget)) {
        return "ElaraListViewWidget";
    }

    if(dynamic_cast<ElaraTreeViewWidget*>(widget)) {
        return "ElaraTreeViewWidget";
    }

    if(dynamic_cast<ElaraRichTextEditWidget*>(widget)) {
        return "ElaraRichTextEditWidget";
    }

    if(dynamic_cast<ElaraMultiAxisLineChartWidget*>(widget)) {
        return "ElaraMultiAxisLineChartWidget";
    }

    return "ElaraWidget";
}

ElaraWidgetState ElaraWidgetStateProbe::widgetState(Ref<ElaraWidget> widget) {
    ElaraWidgetState state;

    if(!widget) {
        return state;
    }

    ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(widget.getPtr());
    if(root) {
        state.kind = ELARA_WIDGET_STATE_ROOT;
        return state;
    }

    ElaraButtonWidget* button = dynamic_cast<ElaraButtonWidget*>(widget.getPtr());
    if(button) {
        state.kind = ELARA_WIDGET_STATE_BUTTON;
        state.text = button->getText();
        state.action = button->getAction();
        state.enabled = button->isEnabled();
        state.has_text = true;
        state.has_action = true;
        state.has_enabled = true;
        return state;
    }

    ElaraCheckboxWidget* checkbox = dynamic_cast<ElaraCheckboxWidget*>(widget.getPtr());
    if(checkbox) {
        state.kind = ELARA_WIDGET_STATE_CHECKBOX;
        state.text = checkbox->getText();
        state.checked = checkbox->isChecked();
        state.enabled = checkbox->isEnabled();
        state.font_size = checkbox->getFontSize();
        state.has_text = true;
        state.has_checked = true;
        state.has_enabled = true;
        state.has_font_size = true;
        return state;
    }

    ElaraRadioButtonWidget* radio = dynamic_cast<ElaraRadioButtonWidget*>(widget.getPtr());
    if(radio) {
        state.kind = ELARA_WIDGET_STATE_RADIO_BUTTON;
        state.text = radio->getText();
        state.group = radio->getGroup();
        state.checked = radio->isChecked();
        state.enabled = radio->isEnabled();
        state.font_size = radio->getFontSize();
        state.has_text = true;
        state.has_group = true;
        state.has_checked = true;
        state.has_enabled = true;
        state.has_font_size = true;
        return state;
    }

    ElaraLabelWidget* label = dynamic_cast<ElaraLabelWidget*>(widget.getPtr());
    if(label) {
        state.kind = ELARA_WIDGET_STATE_LABEL;
        state.text = label->getText();
        state.font_size = label->getFontSize();
        state.draw_background = label->getDrawBackground();
        state.has_text = true;
        state.has_font_size = true;
        state.has_draw_background = true;
        return state;
    }

    ElaraTextInputWidget* input = dynamic_cast<ElaraTextInputWidget*>(widget.getPtr());
    if(input) {
        state.kind = ELARA_WIDGET_STATE_TEXT_INPUT;
        state.text = input->getText();
        state.placeholder = input->getPlaceholder();
        state.enabled = input->isEnabled();
        state.has_text = true;
        state.has_placeholder = true;
        state.has_enabled = true;
        return state;
    }

    ElaraSliderWidget* slider = dynamic_cast<ElaraSliderWidget*>(widget.getPtr());
    if(slider) {
        state.kind = ELARA_WIDGET_STATE_SLIDER;
        state.orientation = slider->getOrientation();
        state.minimum = slider->getMinimum();
        state.maximum = slider->getMaximum();
        state.value = slider->getValue();
        state.step = slider->getStep();
        state.enabled = slider->isEnabled();
        state.has_orientation = true;
        state.has_minimum = true;
        state.has_maximum = true;
        state.has_value = true;
        state.has_step = true;
        state.has_enabled = true;
        return state;
    }

    ElaraSpinnerWidget* spinner = dynamic_cast<ElaraSpinnerWidget*>(widget.getPtr());
    if(spinner) {
        state.kind = ELARA_WIDGET_STATE_SPINNER;
        state.minimum = spinner->getMinimum();
        state.maximum = spinner->getMaximum();
        state.value = spinner->getValue();
        state.step = spinner->getStep();
        state.enabled = spinner->isEnabled();
        state.font_size = spinner->getFontSize();
        state.has_minimum = true;
        state.has_maximum = true;
        state.has_value = true;
        state.has_step = true;
        state.has_enabled = true;
        state.has_font_size = true;
        return state;
    }

    ElaraComboBoxWidget* combo = dynamic_cast<ElaraComboBoxWidget*>(widget.getPtr());
    if(combo) {
        state.kind = ELARA_WIDGET_STATE_COMBO_BOX;
        state.text = combo->getSelectedText();
        state.selected_id = combo->getSelectedId();
        state.enabled = combo->isEnabled();
        state.item_count = combo->getItemCount();
        state.has_text = true;
        state.has_selected_id = true;
        state.has_enabled = true;
        state.has_item_count = true;
        return state;
    }

    ElaraListViewWidget* list = dynamic_cast<ElaraListViewWidget*>(widget.getPtr());
    if(list) {
        state.kind = ELARA_WIDGET_STATE_LIST_VIEW;
        state.text = list->getSelectedText();
        state.selected_id = list->getSelectedId();
        state.enabled = list->isEnabled();
        state.font_size = list->getFontSize();
        state.item_count = list->getItemCount();
        state.has_text = true;
        state.has_selected_id = true;
        state.has_enabled = true;
        state.has_font_size = true;
        state.has_item_count = true;
        return state;
    }

    ElaraTreeViewWidget* tree = dynamic_cast<ElaraTreeViewWidget*>(widget.getPtr());
    if(tree) {
        state.kind = ELARA_WIDGET_STATE_TREE_VIEW;
        state.text = tree->getSelectedText();
        state.selected_id = tree->getSelectedId();
        state.enabled = tree->isEnabled();
        state.font_size = tree->getFontSize();
        state.item_count = tree->getNodeCount();
        state.expanded_count = tree->getExpandedCount();
        state.has_text = true;
        state.has_selected_id = true;
        state.has_enabled = true;
        state.has_font_size = true;
        state.has_item_count = true;
        state.has_expanded_count = true;
        return state;
    }

    ElaraRichTextEditWidget* rich = dynamic_cast<ElaraRichTextEditWidget*>(widget.getPtr());
    if(rich) {
        state.kind = ELARA_WIDGET_STATE_RICH_TEXT_EDIT;
        state.text = rich->getText();
        state.placeholder = rich->getPlaceholder();
        state.font_size = rich->getFontSize();
        state.enabled = rich->isEnabled();
        state.minimum = rich->getScrollX();
        state.maximum = rich->getScrollY();
        state.has_text = true;
        state.has_placeholder = true;
        state.has_font_size = true;
        state.has_enabled = true;
        state.has_scroll_x = true;
        state.has_scroll_y = true;
        return state;
    }

    ElaraPopupWidget* popup = dynamic_cast<ElaraPopupWidget*>(widget.getPtr());
    if(popup) {
        state.kind = ELARA_WIDGET_STATE_POPUP;
        state.popup_visible = popup->isVisible();
        state.item_count = popup->itemCount();
        state.has_popup_visible = true;
        state.has_item_count = true;
        return state;
    }

    ElaraMenuBarWidget* menu_bar = dynamic_cast<ElaraMenuBarWidget*>(widget.getPtr());
    if(menu_bar) {
        state.kind = ELARA_WIDGET_STATE_MENU_BAR;
        state.selected_id = menu_bar->getActiveMenuId();
        state.font_size = menu_bar->getFontSize();
        state.item_count = menu_bar->menuCount();
        state.has_selected_id = true;
        state.has_font_size = true;
        state.has_item_count = true;
        return state;
    }

    ElaraTabWidget* tabs = dynamic_cast<ElaraTabWidget*>(widget.getPtr());
    if(tabs) {
        state.kind = ELARA_WIDGET_STATE_TABS;
        state.active_tab = tabs->getActiveTab();
        state.tab_count = tabs->tabCount();
        state.has_active_tab = true;
        state.has_tab_count = true;
        return state;
    }

    ElaraGridLayout* grid = dynamic_cast<ElaraGridLayout*>(widget.getPtr());
    if(grid) {
        state.kind = ELARA_WIDGET_STATE_GRID;
        state.layout = "grid";
        state.has_layout = true;
        return state;
    }

    ElaraMultiAxisLineChartWidget* chart = dynamic_cast<ElaraMultiAxisLineChartWidget*>(widget.getPtr());
    if(chart) {
        state.kind = ELARA_WIDGET_STATE_MULTI_AXIS_LINE_CHART;
        state.text = chart->getTitle();
        state.axis_count = chart->axisCount();
        state.series_count = chart->seriesCount();
        state.has_text = true;
        state.has_axis_count = true;
        state.has_series_count = true;
        return state;
    }

    return state;
}

ElaraWidgetSnapshot ElaraWidgetStateProbe::widgetSnapshot(Ref<ElaraWidget> widget) {
    ElaraWidgetSnapshot snapshot;

    if(!widget) {
        return snapshot;
    }

    snapshot.exists = true;
    snapshot.id = widgetHandleToString(widget->getHandle());
    snapshot.type = widgetTypeName(widget.getPtr());
    snapshot.visible = widget->isVisible();

    snapshot.bounds.x = widget->getX();
    snapshot.bounds.y = widget->getY();
    snapshot.bounds.width = widget->getWidth();
    snapshot.bounds.height = widget->getHeight();

    snapshot.absolute_bounds.x = widget->getAbsoluteX();
    snapshot.absolute_bounds.y = widget->getAbsoluteY();
    snapshot.absolute_bounds.width = widget->getWidth();
    snapshot.absolute_bounds.height = widget->getHeight();

    snapshot.state = widgetState(widget);

    for(int i = 0; i < widget->childCount(); i++) {
        snapshot.children.push(widgetSnapshot(widget->getChild(i)));
    }

    return snapshot;
}

ElaraRootSnapshot ElaraWidgetStateProbe::rootSnapshot(ElaraRootWidget* root) {
    ElaraRootSnapshot snapshot;

    if(!root) {
        return snapshot;
    }

    snapshot.content = widgetSnapshot(root->getContent());
    snapshot.popup = widgetSnapshot(root->getPopup());
    for(int i = 0; i < root->popupCount(); i++) {
        snapshot.popups.push(widgetSnapshot(root->getPopup(i)));
    }
    snapshot.focus = widgetHandleToString(root->getFocus());
    return snapshot;
}

String ElaraWidgetStateProbe::widgetStateJson(const ElaraWidgetState& state) {
    String json("{");
    bool has_field = false;

    if(state.has_text) {
        json += jsonStringField("text", state.text, has_field);
        has_field = true;
    }

    if(state.has_action) {
        json += jsonStringField("action", state.action, has_field);
        has_field = true;
    }

    if(state.has_group) {
        json += jsonStringField("group", state.group, has_field);
        has_field = true;
    }

    if(state.has_selected_id) {
        json += jsonStringField("selectedId", state.selected_id, has_field);
        has_field = true;
    }

    if(state.has_placeholder) {
        json += jsonStringField("placeholder", state.placeholder, has_field);
        has_field = true;
    }

    if(state.has_enabled) {
        json += jsonBoolField("enabled", state.enabled, has_field);
        has_field = true;
    }

    if(state.has_checked) {
        json += jsonBoolField("checked", state.checked, has_field);
        has_field = true;
    }

    if(state.has_font_size) {
        json += jsonDoubleField("fontSize", state.font_size, has_field);
        has_field = true;
    }

    if(state.has_draw_background) {
        json += jsonBoolField("drawBackground", state.draw_background, has_field);
        has_field = true;
    }

    if(state.has_popup_visible) {
        json += jsonBoolField("visible", state.popup_visible, has_field);
        has_field = true;
    }

    if(state.has_active_tab) {
        json += jsonIntField("activeTab", state.active_tab, has_field);
        has_field = true;
    }

    if(state.has_tab_count) {
        json += jsonIntField("tabCount", state.tab_count, has_field);
        has_field = true;
    }

    if(state.has_axis_count) {
        json += jsonIntField("axisCount", state.axis_count, has_field);
        has_field = true;
    }

    if(state.has_series_count) {
        json += jsonIntField("seriesCount", state.series_count, has_field);
        has_field = true;
    }

    if(state.has_item_count) {
        json += jsonIntField("itemCount", state.item_count, has_field);
        has_field = true;
    }

    if(state.has_expanded_count) {
        json += jsonIntField("expandedCount", state.expanded_count, has_field);
        has_field = true;
    }

    if(state.has_layout) {
        json += jsonStringField("layout", state.layout, has_field);
        has_field = true;
    }

    if(state.has_orientation) {
        json += jsonStringField("orientation", state.orientation, has_field);
        has_field = true;
    }

    if(state.has_minimum) {
        json += jsonDoubleField("min", state.minimum, has_field);
        has_field = true;
    }

    if(state.has_maximum) {
        json += jsonDoubleField("max", state.maximum, has_field);
        has_field = true;
    }

    if(state.has_value) {
        json += jsonDoubleField("value", state.value, has_field);
        has_field = true;
    }

    if(state.has_step) {
        json += jsonDoubleField("step", state.step, has_field);
        has_field = true;
    }

    if(state.has_scroll_x) {
        json += jsonIntField("scrollX", (int)state.minimum, has_field);
        has_field = true;
    }

    if(state.has_scroll_y) {
        json += jsonIntField("scrollY", (int)state.maximum, has_field);
        has_field = true;
    }

    json += "}";
    return json;
}

String ElaraWidgetStateProbe::widgetSnapshotJson(const ElaraWidgetSnapshot& snapshot) {
    if(!snapshot.exists) {
        return "null";
    }

    String json =
        String("{\"id\":\"") +
        JsonString::encode(snapshot.id) +
        String("\",\"type\":\"") +
        JsonString::encode(snapshot.type) +
        String("\",\"visible\":") +
        jsonBoolean(snapshot.visible) +
        String(",\"bounds\":{\"x\":") +
        String(snapshot.bounds.x) +
        String(",\"y\":") +
        String(snapshot.bounds.y) +
        String(",\"width\":") +
        String(snapshot.bounds.width) +
        String(",\"height\":") +
        String(snapshot.bounds.height) +
        String("},\"absoluteBounds\":{\"x\":") +
        String(snapshot.absolute_bounds.x) +
        String(",\"y\":") +
        String(snapshot.absolute_bounds.y) +
        String(",\"width\":") +
        String(snapshot.absolute_bounds.width) +
        String(",\"height\":") +
        String(snapshot.absolute_bounds.height) +
        String("},\"state\":") +
        widgetStateJson(snapshot.state) +
        String(",\"children\":[");

    for(int i = 0; i < snapshot.children.length(); i++) {
        if(i > 0) {
            json += ",";
        }

        json += widgetSnapshotJson(snapshot.children[i]);
    }

    json += "]}";
    return json;
}

String ElaraWidgetStateProbe::rootSnapshotJson(const ElaraRootSnapshot& snapshot) {
    String json = String("{\"content\":") +
        widgetSnapshotJson(snapshot.content) +
        String(",\"popup\":") +
        widgetSnapshotJson(snapshot.popup) +
        String(",\"popups\":[");

    for(int i = 0; i < snapshot.popups.length(); i++) {
        if(i > 0) {
            json += ",";
        }

        json += widgetSnapshotJson(snapshot.popups[i]);
    }

    json += String("],\"focus\":\"") +
        JsonString::encode(snapshot.focus) +
        String("\"}");

    return json;
}

}

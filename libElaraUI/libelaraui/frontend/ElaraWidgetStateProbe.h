#ifndef ELARA_WIDGET_STATE_PROBE_H
#define ELARA_WIDGET_STATE_PROBE_H

#include <libelaracore/memory/Array.h>

#include "ElaraWidgetHandle.h"

namespace elara {

class ElaraWidget;
class ElaraRootWidget;

enum ElaraWidgetStateKind {
    ELARA_WIDGET_STATE_UNKNOWN = 0,
    ELARA_WIDGET_STATE_ROOT,
    ELARA_WIDGET_STATE_MENU_BAR,
    ELARA_WIDGET_STATE_TABS,
    ELARA_WIDGET_STATE_POPUP,
    ELARA_WIDGET_STATE_GRID,
    ELARA_WIDGET_STATE_BUTTON,
    ELARA_WIDGET_STATE_CHECKBOX,
    ELARA_WIDGET_STATE_RADIO_BUTTON,
    ELARA_WIDGET_STATE_LABEL,
    ELARA_WIDGET_STATE_TEXT_INPUT,
    ELARA_WIDGET_STATE_SLIDER,
    ELARA_WIDGET_STATE_SPINNER,
    ELARA_WIDGET_STATE_LIST_VIEW,
    ELARA_WIDGET_STATE_TREE_VIEW,
    ELARA_WIDGET_STATE_CODE_EDITOR,
    ELARA_WIDGET_STATE_RICH_TEXT_EDIT,
    ELARA_WIDGET_STATE_MULTI_AXIS_LINE_CHART,
    ELARA_WIDGET_STATE_COMBO_BOX
};

struct ElaraWidgetBounds {
    double x;
    double y;
    double width;
    double height;

    ElaraWidgetBounds()
        : x(0),
          y(0),
          width(0),
          height(0) {
    }
};

struct ElaraWidgetState {
    ElaraWidgetStateKind kind;

    String text;
    String action;
    String group;
    String selected_id;
    String placeholder;
    String layout;
    String orientation;

    bool enabled;
    bool checked;
    bool draw_background;
    bool popup_visible;

    double font_size;
    double minimum;
    double maximum;
    double value;
    double step;

    int active_tab;
    int tab_count;
    int axis_count;
    int series_count;
    int item_count;
    int expanded_count;

    bool has_text;
    bool has_action;
    bool has_group;
    bool has_selected_id;
    bool has_placeholder;
    bool has_enabled;
    bool has_checked;
    bool has_draw_background;
    bool has_popup_visible;
    bool has_font_size;
    bool has_active_tab;
    bool has_tab_count;
    bool has_axis_count;
    bool has_series_count;
    bool has_item_count;
    bool has_expanded_count;
    bool has_layout;
    bool has_orientation;
    bool has_minimum;
    bool has_maximum;
    bool has_value;
    bool has_step;
    bool has_scroll_x;
    bool has_scroll_y;

    ElaraWidgetState()
        : kind(ELARA_WIDGET_STATE_UNKNOWN),
          enabled(false),
          checked(false),
          draw_background(false),
          popup_visible(false),
          font_size(0),
          minimum(0),
          maximum(0),
          value(0),
          step(0),
          active_tab(0),
          tab_count(0),
          axis_count(0),
          series_count(0),
          item_count(0),
          expanded_count(0),
          has_text(false),
          has_action(false),
          has_group(false),
          has_selected_id(false),
          has_placeholder(false),
          has_enabled(false),
          has_checked(false),
          has_draw_background(false),
          has_popup_visible(false),
          has_font_size(false),
          has_active_tab(false),
          has_tab_count(false),
          has_axis_count(false),
          has_series_count(false),
          has_item_count(false),
          has_expanded_count(false),
          has_layout(false),
          has_orientation(false),
          has_minimum(false),
          has_maximum(false),
          has_value(false),
          has_step(false),
          has_scroll_x(false),
          has_scroll_y(false) {
    }
};

struct ElaraWidgetSnapshot {
    String id;
    String type;
    bool visible;
    ElaraWidgetBounds bounds;
    ElaraWidgetBounds absolute_bounds;
    ElaraWidgetState state;
    Array<ElaraWidgetSnapshot> children;
    bool exists;

    ElaraWidgetSnapshot()
        : visible(false),
          children(),
          exists(false) {
    }
};

struct ElaraRootSnapshot {
    ElaraWidgetSnapshot content;
    ElaraWidgetSnapshot popup;
    Array<ElaraWidgetSnapshot> popups;
    String focus;
};

class ElaraWidgetStateProbe {
public:
    static String widgetHandleToString(ElaraWidgetHandle handle);
    static String widgetTypeName(ElaraWidget* widget);
    static ElaraWidgetState widgetState(Ref<ElaraWidget> widget);
    static ElaraWidgetSnapshot widgetSnapshot(Ref<ElaraWidget> widget);
    static ElaraRootSnapshot rootSnapshot(ElaraRootWidget* root);

    static String widgetStateJson(const ElaraWidgetState& state);
    static String widgetSnapshotJson(const ElaraWidgetSnapshot& snapshot);
    static String rootSnapshotJson(const ElaraRootSnapshot& snapshot);
};

}

#endif

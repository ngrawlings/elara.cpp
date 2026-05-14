import json
from copy import deepcopy


class UiDocumentBuilder:
    def __init__(self):
        self.clear()

    def clear(self):
        self.window_title = "Elara UI"
        self.window_width = 800
        self.window_height = 600
        self.window_min_width = 0
        self.window_min_height = 0
        self.window_backend_id = "org.elara.ui.app"
        self.window_properties = {}
        self.theme_mode = "light"
        self.root_content = None
        self.root_popups = []
        self._widgets = {}
        self._widget_order = []

    def create_window(self, title, width, height, backend_id):
        self.window_title = title
        self.window_width = int(width)
        self.window_height = int(height)
        self.window_min_width = 0
        self.window_min_height = 0
        self.window_backend_id = backend_id
        return self

    def set_minimum_window_size(self, w, h):
        self.window_min_width = int(w)
        self.window_min_height = int(h)
        return self

    def set_theme_mode(self, mode):
        self.theme_mode = mode
        return self

    def set_window_property(self, name, value):
        self.window_properties[str(name)] = value
        return self

    def set_root_content(self, widget_id):
        self.root_content = widget_id
        return self

    def clear_root_popups(self):
        self.root_popups = []
        return self

    def push_root_popup(self, widget_id):
        if widget_id not in self.root_popups:
            self.root_popups.append(widget_id)
        return self

    def has_widget(self, widget_id):
        return widget_id in self._widgets

    def create_widget(self, widget_id, widget_type):
        if not widget_id or not widget_type:
            raise ValueError("widget_id and widget_type are required")
        if self.has_widget(widget_id):
            raise ValueError(f"widget already exists: {widget_id}")

        self._widgets[widget_id] = {
            "id": widget_id,
            "type": widget_type,
            "properties": {},
            "sections": {},
            "children": [],
            "grid_children": [],
            "tabs": [],
            "popup_items": [],
            "toolbar_items": [],
            "grid_columns": [],
            "grid_rows": [],
        }
        self._widget_order.append(widget_id)
        return self

    def create_tabs(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.tabs")

    def create_popup(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.popup")

    def create_menu_bar(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.menu_bar")

    def create_grid(self, widget_id):
        return self.create_widget(widget_id, "elara.layouts.grid")

    def create_button(self, widget_id, text, action):
        self.create_widget(widget_id, "elara.widgets.button")
        return self.set_property_string(widget_id, "text", text).set_property_string(widget_id, "action", action)

    def create_checkbox(self, widget_id, text, checked):
        self.create_widget(widget_id, "elara.widgets.checkbox")
        return self.set_property_string(widget_id, "text", text).set_property_bool(widget_id, "checked", checked)

    def create_radio_button(self, widget_id, text, group, checked):
        self.create_widget(widget_id, "elara.widgets.radio_button")
        return (
            self.set_property_string(widget_id, "text", text)
            .set_property_string(widget_id, "group", group)
            .set_property_bool(widget_id, "checked", checked)
        )

    def create_label(self, widget_id, text, font_size):
        self.create_widget(widget_id, "elara.widgets.label")
        return self.set_property_string(widget_id, "text", text).set_property_number(widget_id, "font_size", font_size)

    def create_text_input(self, widget_id, placeholder="", text=""):
        self.create_widget(widget_id, "elara.widgets.text_input")
        if placeholder:
            self.set_property_string(widget_id, "placeholder", placeholder)
        if text:
            self.set_property_string(widget_id, "text", text)
        return self

    def create_slider(self, widget_id, orientation, min_value, max_value, value, step):
        self.create_widget(widget_id, "elara.widgets.slider")
        return (
            self.set_property_string(widget_id, "orientation", orientation)
            .set_property_number(widget_id, "min", min_value)
            .set_property_number(widget_id, "max", max_value)
            .set_property_number(widget_id, "value", value)
            .set_property_number(widget_id, "step", step)
        )

    def create_spinner(self, widget_id, min_value, max_value, value, step):
        self.create_widget(widget_id, "elara.widgets.spinner")
        return (
            self.set_property_number(widget_id, "min", min_value)
            .set_property_number(widget_id, "max", max_value)
            .set_property_number(widget_id, "value", value)
            .set_property_number(widget_id, "step", step)
        )

    def create_list_view(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.list_view")

    def create_combo_box(self, widget_id, items=None, selected_id=None):
        """items: list of {"id": ..., "label": ...} dicts"""
        self.create_widget(widget_id, "elara.widgets.combo_box")
        if items:
            self.set_section_json(widget_id, "items", items)
        if selected_id is not None:
            self.set_property_string(widget_id, "selected_id", selected_id)
        return self

    def create_tree_view(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.tree_view")

    def create_rich_text_edit(self, widget_id, text):
        self.create_widget(widget_id, "elara.widgets.rich_text_edit")
        return self.set_property_string(widget_id, "text", text)

    def create_chat_dialog(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.chat_dialog")

    def create_toolbar(self, widget_id, orientation="vertical"):
        self.create_widget(widget_id, "elara.widgets.toolbar")
        self.set_property_string(widget_id, "orientation", orientation)
        return self

    def add_toolbar_item(self, widget_id, item_id, text, icon="", enabled=True, tooltip=""):
        widget = self._get_widget(widget_id)
        item = {"id": item_id, "text": text}
        if icon:
            item["icon"] = icon
        if not enabled:
            item["enabled"] = False
        if tooltip:
            item["tooltip"] = tooltip
        widget["toolbar_items"].append(item)
        return self

    def add_toolbar_separator(self, widget_id):
        self._get_widget(widget_id)["toolbar_items"].append({"separator": True})
        return self

    def create_code_editor(self, widget_id, text="", font_size=14):
        self.create_widget(widget_id, "elara.widgets.code_editor")
        if text:
            self.set_property_string(widget_id, "text", text)
        self.set_property_number(widget_id, "font_size", font_size)
        return self

    def create_surface_panel(self, widget_id):
        return self.create_widget(widget_id, "demo.widgets.surface_panel")

    def create_opencl_surface(self, widget_id, commands=None, backend="opencl", kernel_name=None):
        self.create_widget(widget_id, "elara.widgets.opencl_surface")
        self.set_property_string(widget_id, "backend", backend)
        if kernel_name:
            self.set_property_string(widget_id, "kernel_name", kernel_name)
        if commands is not None:
            self.set_section_json(widget_id, "commands", commands)
        return self

    def create_density_map(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.density_map")

    def create_multi_axis_line_chart(self, widget_id):
        return self.create_widget(widget_id, "elara.widgets.multi_axis_line_chart")

    def add_child(self, parent_id, child_id):
        parent = self._get_widget(parent_id)
        self._get_widget(child_id)
        self._detach_child_reference(child_id)
        parent["children"].append({"child_id": child_id})
        return self

    def add_tab(self, tabs_id, title, child_id, button_glyph=None, button_action=None):
        tabs = self._get_widget(tabs_id)
        self._get_widget(child_id)
        self._detach_child_reference(child_id)
        entry = {"title": title, "child_id": child_id}
        if button_glyph:
            entry["button_glyph"] = button_glyph
        if button_action:
            entry["button_action"] = button_action
        tabs["tabs"].append(entry)
        return self

    def add_popup_item(self, popup_id, item_id, label):
        popup = self._get_widget(popup_id)
        popup["popup_items"].append({"id": item_id, "label": label})
        return self

    def add_menu_bar_menu(self, menu_bar_id, menu_id, label):
        menu_bar = self._get_widget(menu_bar_id)
        menus = list(menu_bar["sections"].get("menus", []))

        for menu in menus:
            if menu.get("id") == menu_id:
                menu["label"] = label
                menu.setdefault("items", [])
                break
        else:
            menus.append({"id": menu_id, "label": label, "items": []})

        menu_bar["sections"]["menus"] = menus
        return self

    def set_menu_bar_menus(self, menu_bar_id, menus):
        menu_bar = self._get_widget(menu_bar_id)
        menu_bar["sections"]["menus"] = deepcopy(menus)
        return self

    def add_menu_bar_separator(self, menu_bar_id, menu_id):
        menu_bar = self._get_widget(menu_bar_id)
        menus = list(menu_bar["sections"].get("menus", []))

        for menu in menus:
            if menu.get("id") != menu_id:
                continue
            menu.setdefault("items", [])
            menu["items"].append({"separator": True})
            break
        else:
            menus.append(
                {
                    "id": menu_id,
                    "label": menu_id,
                    "items": [{"separator": True}],
                }
            )

        menu_bar["sections"]["menus"] = menus
        return self

    def add_menu_bar_item(self, menu_bar_id, menu_id, item_id, label, enabled=True, shortcut=None):
        menu_bar = self._get_widget(menu_bar_id)
        menus = list(menu_bar["sections"].get("menus", []))

        for menu in menus:
            if menu.get("id") != menu_id:
                continue
            menu.setdefault("items", [])
            for item in menu["items"]:
                if item.get("id") == item_id:
                    item["label"] = label
                    item["enabled"] = bool(enabled)
                    if shortcut:
                        item["shortcut"] = shortcut
                    elif "shortcut" in item:
                        del item["shortcut"]
                    break
            else:
                item = {"id": item_id, "label": label, "enabled": bool(enabled)}
                if shortcut:
                    item["shortcut"] = shortcut
                menu["items"].append(item)
            break
        else:
            item = {"id": item_id, "label": label, "enabled": bool(enabled)}
            if shortcut:
                item["shortcut"] = shortcut
            menus.append(
                {
                    "id": menu_id,
                    "label": menu_id,
                    "items": [item],
                }
            )

        menu_bar["sections"]["menus"] = menus
        return self

    def add_grid_column_exact(self, grid_id, size):
        grid = self._get_widget(grid_id)
        grid["grid_columns"].append({"fill": False, "size": size, "weight": 1.0, "resizable": False})
        return self

    def add_grid_column_fill(self, grid_id):
        grid = self._get_widget(grid_id)
        grid["grid_columns"].append({"fill": True, "size": 0, "weight": 1.0, "resizable": False})
        return self

    def add_grid_column_weighted_fill(self, grid_id, weight):
        grid = self._get_widget(grid_id)
        grid["grid_columns"].append({"fill": True, "size": 0, "weight": float(weight) if weight and weight > 0 else 1.0, "resizable": False})
        return self

    def set_grid_column_border_resizable(self, grid_id, index, enabled=True):
        grid = self._get_widget(grid_id)
        if index < 0 or index >= len(grid["grid_columns"]):
            raise IndexError("grid column index out of range")
        grid["grid_columns"][int(index)]["resizable"] = bool(enabled)
        return self

    def add_grid_row_exact(self, grid_id, size):
        grid = self._get_widget(grid_id)
        grid["grid_rows"].append({"fill": False, "size": size, "weight": 1.0, "resizable": False})
        return self

    def add_grid_row_fill(self, grid_id):
        grid = self._get_widget(grid_id)
        grid["grid_rows"].append({"fill": True, "size": 0, "weight": 1.0, "resizable": False})
        return self

    def add_grid_row_weighted_fill(self, grid_id, weight):
        grid = self._get_widget(grid_id)
        grid["grid_rows"].append({"fill": True, "size": 0, "weight": float(weight) if weight and weight > 0 else 1.0, "resizable": False})
        return self

    def set_grid_row_border_resizable(self, grid_id, index, enabled=True):
        grid = self._get_widget(grid_id)
        if index < 0 or index >= len(grid["grid_rows"]):
            raise IndexError("grid row index out of range")
        grid["grid_rows"][int(index)]["resizable"] = bool(enabled)
        return self

    def place_grid_child(self, grid_id, child_id, column, row, column_span=1, row_span=1):
        grid = self._get_widget(grid_id)
        self._get_widget(child_id)
        self._detach_child_reference(child_id)
        grid["grid_children"].append(
            {
                "child_id": child_id,
                "column": int(column),
                "row": int(row),
                "column_span": int(column_span),
                "row_span": int(row_span),
            }
        )
        return self

    def set_property_string(self, widget_id, name, value):
        self._get_widget(widget_id)["properties"][name] = value
        return self

    def set_property_number(self, widget_id, name, value):
        self._get_widget(widget_id)["properties"][name] = value
        return self

    def set_property_bool(self, widget_id, name, value):
        self._get_widget(widget_id)["properties"][name] = bool(value)
        return self

    def set_property_json(self, widget_id, name, raw_json_or_value):
        value = self._coerce_json_value(raw_json_or_value)
        self._get_widget(widget_id)["properties"][name] = value
        return self

    def set_section_json(self, widget_id, section_name, raw_json_or_value):
        value = self._coerce_json_value(raw_json_or_value)
        self._get_widget(widget_id)["sections"][section_name] = value
        return self


    def snapshot_client_sections(self):
        """Return static client-side sections by widget id for snapshot augmentation.

        The C++ runtime snapshot is intentionally sparse for complex controls.
        This helper preserves builder-side payloads such as list items, tree nodes,
        menu definitions, toolbar items, chart data, and custom sections so a
        dumped snapshot can include the full logical widget state known by Python.
        """
        result = {}
        for widget_id, widget in self._widgets.items():
            sections = {}
            if widget.get("sections"):
                sections.update(deepcopy(widget["sections"]))
            if widget.get("popup_items"):
                sections["items"] = deepcopy(widget["popup_items"])
            if widget.get("toolbar_items"):
                sections["items"] = deepcopy(widget["toolbar_items"])
            if widget.get("properties"):
                sections["properties"] = deepcopy(widget["properties"])
            if sections:
                result[widget_id] = sections
        return result

    def widget_dict(self, widget_id):
        return self._serialize_widget(self._get_widget(widget_id))

    def widget_json(self, widget_id, indent=2):
        return json.dumps(self.widget_dict(widget_id), indent=indent, ensure_ascii=False)

    def to_dict(self):
        root = {"content": self.root_content or ""}
        if self.root_popups:
            root["popup"] = self.root_popups[0]
            root["popups"] = list(self.root_popups)

        widgets = []
        for widget_id in self._widget_order:
            if self._is_nested_widget(widget_id):
                continue
            widgets.append(self._serialize_widget(self._widgets[widget_id]))

        return {
            "elara_ui_protocol": 1,
            "window": {
                "title": self.window_title,
                "width": self.window_width,
                "height": self.window_height,
                "min_width": self.window_min_width,
                "min_height": self.window_min_height,
                "backend_id": self.window_backend_id,
                **deepcopy(self.window_properties),
            },
            "theme": {"mode": self.theme_mode},
            "root": root,
            "widgets": widgets,
        }

    def to_json(self, indent=2):
        return json.dumps(self.to_dict(), indent=indent, ensure_ascii=False)

    def load_document_params(self):
        return {"document": self.to_json(indent=2)}

    def _get_widget(self, widget_id):
        widget = self._widgets.get(widget_id)
        if widget is None:
            raise KeyError(f"unknown widget: {widget_id}")
        return widget

    def _detach_child_reference(self, child_id):
        for widget in self._widgets.values():
            widget["children"] = [item for item in widget["children"] if item["child_id"] != child_id]
            widget["grid_children"] = [item for item in widget["grid_children"] if item["child_id"] != child_id]
            widget["tabs"] = [item for item in widget["tabs"] if item["child_id"] != child_id]

    def _is_nested_widget(self, widget_id):
        for widget in self._widgets.values():
            for item in widget["children"]:
                if item["child_id"] == widget_id:
                    return True
            for item in widget["grid_children"]:
                if item["child_id"] == widget_id:
                    return True
            for item in widget["tabs"]:
                if item["child_id"] == widget_id:
                    return True
        return False

    def _serialize_widget(self, widget):
        result = {
            "id": widget["id"],
            "type": widget["type"],
        }

        if widget["properties"]:
            result["properties"] = deepcopy(widget["properties"])

        if widget["sections"]:
            result["sections"] = deepcopy(widget["sections"])
            # Keep legacy flattened section fields for existing widgets that
            # still read arrays like `items`, `nodes`, or `menus` at top level.
            for name, value in widget["sections"].items():
                result[name] = deepcopy(value)

        if widget["popup_items"]:
            result["items"] = deepcopy(widget["popup_items"])
        elif widget.get("toolbar_items"):
            result["items"] = deepcopy(widget["toolbar_items"])

        if widget["grid_columns"]:
            result["columns"] = [
                (
                    dict(
                        {"mode": "fill", "weight": column.get("weight", 1.0)}
                        if column.get("weight", 1.0) != 1.0 else {"mode": "fill"},
                        **({"resizable": True} if column.get("resizable") else {})
                    )
                ) if column["fill"] else dict(
                    {"mode": "exact", "size": column["size"]},
                    **({"resizable": True} if column.get("resizable") else {})
                )
                for column in widget["grid_columns"]
            ]

        if widget["grid_rows"]:
            result["rows"] = [
                (
                    dict(
                        {"mode": "fill", "weight": row.get("weight", 1.0)}
                        if row.get("weight", 1.0) != 1.0 else {"mode": "fill"},
                        **({"resizable": True} if row.get("resizable") else {})
                    )
                ) if row["fill"] else dict(
                    {"mode": "exact", "size": row["size"]},
                    **({"resizable": True} if row.get("resizable") else {})
                )
                for row in widget["grid_rows"]
            ]

        if widget["tabs"]:
            result["tabs"] = []
            for tab in widget["tabs"]:
                tab_dict = {
                    "title": tab["title"],
                    "widget": self._serialize_widget(self._get_widget(tab["child_id"])),
                }
                if "button_glyph" in tab:
                    tab_dict["button_glyph"] = tab["button_glyph"]
                if "button_action" in tab:
                    tab_dict["button_action"] = tab["button_action"]
                result["tabs"].append(tab_dict)
        elif widget["grid_children"]:
            result["children"] = []
            for child in widget["grid_children"]:
                child_widget = self._serialize_widget(self._get_widget(child["child_id"]))
                child_widget["cell"] = {
                    "column": child["column"],
                    "row": child["row"],
                    "column_span": child["column_span"],
                    "row_span": child["row_span"],
                }
                result["children"].append(child_widget)
        elif widget["children"]:
            result["children"] = [
                self._serialize_widget(self._get_widget(child["child_id"]))
                for child in widget["children"]
            ]

        return result

    @staticmethod
    def _coerce_json_value(raw_json_or_value):
        if isinstance(raw_json_or_value, str):
            try:
                return json.loads(raw_json_or_value)
            except json.JSONDecodeError:
                return raw_json_or_value
        return deepcopy(raw_json_or_value)

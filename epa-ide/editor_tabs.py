from pathlib import Path

from elara_ui.builder import UiDocumentBuilder


def _editor_ids(tab_id: str):
    return {
        "container": f"{tab_id}.container",
        "toolbar": f"{tab_id}.toolbar",
        "button_e": f"{tab_id}.view.e",
        "button_epa": f"{tab_id}.view.epa",
        "source": f"{tab_id}.source",
        "epa": f"{tab_id}.epa",
        "debug_panel": f"{tab_id}.debug.panel",
        "debug": f"{tab_id}.debug.trace",
        "debug_tabs": f"{tab_id}.debug.tabs",
        "debug_ghs": f"{tab_id}.debug.ghs",
        "debug_stack": f"{tab_id}.debug.stack",
        "debug_local": f"{tab_id}.debug.local",
        "debug_dynamic": f"{tab_id}.debug.dynamic",
    }


_PROJECT_TOOLBAR_ITEMS = [
    ("toolbar.files", "Files"),
    ("toolbar.search", "Search"),
    ("toolbar.repo", "Repo"),
    ("toolbar.issues", "Issues"),
    None,
    ("toolbar.debug", "Debug"),
]


def _project_toolbar_items(enabled: bool):
    items = []
    for entry in _PROJECT_TOOLBAR_ITEMS:
        if entry is None:
            items.append({"separator": True})
        else:
            item_id, text = entry
            item = {"id": item_id, "text": text}
            if not enabled:
                item["enabled"] = False
            items.append(item)
    return items


def _set_project_toolbar_enabled(client, enabled: bool):
    client.call("ui.setSectionJson", {
        "target": "app.toolbar",
        "section": "items",
        "value": _project_toolbar_items(enabled),
    })


def _editor_language_for_path(path: str) -> str:
    ext = Path(path).suffix.lower()
    if ext in (".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh"):
        return "cpp"
    if ext == ".py":
        return "python"
    if ext == ".e":
        return "e"
    if ext in (".epa", ".epaasm"):
        return "epa"
    return "plain"


def _focus_editor_widget(client, tab_id: str, state: dict = None):
    if state is None:
        state = {}
    ids = _editor_ids(tab_id)
    view = state.get("view", "e")
    target = tab_id + ".container"
    if state:
        target = ids["source"]
        if view == "epa":
            target = ids["epa"]
    try:
        client.set_focus(target)
    except Exception:
        pass


def _create_e_tab(ui: UiDocumentBuilder, tab_id: str, title: str, source_text: str):
    ids = _editor_ids(tab_id)
    ui.create_grid(ids["container"])
    ui.add_grid_column_fill(ids["container"])
    ui.add_grid_row_exact(ids["container"], 34)
    ui.add_grid_row_fill(ids["container"])

    ui.create_grid(ids["toolbar"])
    ui.add_grid_column_exact(ids["toolbar"], 54)
    ui.add_grid_column_exact(ids["toolbar"], 64)
    ui.add_grid_column_fill(ids["toolbar"])
    ui.add_grid_row_fill(ids["toolbar"])
    ui.create_button(ids["button_e"], "E", f"{ids['button_e']}")
    ui.create_button(ids["button_epa"], "EPA", f"{ids['button_epa']}")
    ui.set_property_bool(ids["button_e"], "enabled", False)
    ui.place_grid_child(ids["toolbar"], ids["button_e"], 0, 0)
    ui.place_grid_child(ids["toolbar"], ids["button_epa"], 1, 0)
    ui.place_grid_child(ids["container"], ids["toolbar"], 0, 0)

    ui.create_grid(ids["debug_panel"])
    ui.add_grid_column_weighted_fill(ids["debug_panel"], 2)
    ui.add_grid_column_exact(ids["debug_panel"], 0)
    ui.set_grid_column_border_resizable(ids["debug_panel"], 0, True)
    ui.add_grid_row_fill(ids["debug_panel"])

    ui.create_code_editor(ids["source"], source_text)
    ui.create_code_editor(ids["epa"], "")
    ui.set_property_string(ids["source"], "language", "e")
    ui.set_property_string(ids["epa"], "language", "epa")
    ui.set_property_bool(ids["epa"], "read_only", True)
    ui.set_property_bool(ids["epa"], "visible", False)

    ui.create_tabs(ids["debug_tabs"])
    ui.create_tree_view(ids["debug"])
    ui.create_tree_view(ids["debug_ghs"])
    ui.create_tree_view(ids["debug_stack"])
    ui.create_tree_view(ids["debug_local"])
    ui.create_tree_view(ids["debug_dynamic"])
    ui.set_section_json(ids["debug"], "nodes", [{"id": f"{ids['debug']}.root", "label": "Stack (LIFO)", "expanded": True, "children": [{"id": f"{ids['debug']}.root.empty", "label": "Stack empty"}]}])
    ui.set_section_json(ids["debug_ghs"], "nodes", [{"id": f"{ids['debug_ghs']}.root", "label": "GHS Layout", "expanded": True}])
    ui.set_section_json(ids["debug_stack"], "nodes", [{"id": f"{ids['debug_stack']}.root", "label": "Stack Interpretation", "expanded": True}])
    ui.set_section_json(ids["debug_local"], "nodes", [{"id": f"{ids['debug_local']}.root", "label": "Local Arena", "expanded": True}])
    ui.set_section_json(ids["debug_dynamic"], "nodes", [{"id": f"{ids['debug_dynamic']}.root", "label": "Dynamic Memory", "expanded": True}])
    ui.add_tab(ids["debug_tabs"], "Trace", ids["debug"])
    ui.add_tab(ids["debug_tabs"], "GHS", ids["debug_ghs"])
    ui.add_tab(ids["debug_tabs"], "Stack", ids["debug_stack"])
    ui.add_tab(ids["debug_tabs"], "Local Arena", ids["debug_local"])
    ui.add_tab(ids["debug_tabs"], "Dynamic", ids["debug_dynamic"])
    ui.place_grid_child(ids["debug_panel"], ids["source"], 0, 0)
    ui.place_grid_child(ids["debug_panel"], ids["epa"], 0, 0)
    ui.place_grid_child(ids["debug_panel"], ids["debug_tabs"], 1, 0)
    ui.place_grid_child(ids["container"], ids["debug_panel"], 0, 1)
    ui.add_tab("editor.tabs", title, ids["container"],
               button_glyph="×", button_action=f"tab.close.{tab_id}")


def _create_python_tab(ui: UiDocumentBuilder, tab_id: str, title: str, source_text: str):
    ids = _editor_ids(tab_id)
    ui.create_grid(ids["container"])
    ui.add_grid_column_fill(ids["container"])
    ui.add_grid_row_fill(ids["container"])

    ui.create_grid(ids["debug_panel"])
    ui.add_grid_column_weighted_fill(ids["debug_panel"], 2)
    ui.add_grid_column_exact(ids["debug_panel"], 0)
    ui.set_grid_column_border_resizable(ids["debug_panel"], 0, True)
    ui.add_grid_row_fill(ids["debug_panel"])

    ui.create_code_editor(ids["source"], source_text)
    ui.set_property_string(ids["source"], "language", "python")
    ui.set_property_number(ids["source"], "font_size", 13)

    ui.create_tabs(ids["debug_tabs"])
    ui.create_list_view(ids["debug_stack"])
    ui.create_list_view(ids["debug_local"])
    ui.set_property_number(ids["debug_stack"], "font_size", 11)
    ui.set_property_number(ids["debug_local"], "font_size", 11)
    ui.set_section_json(ids["debug_stack"], "items", [])
    ui.set_section_json(ids["debug_local"], "items", [])
    ui.add_tab(ids["debug_tabs"], "Stack", ids["debug_stack"])
    ui.add_tab(ids["debug_tabs"], "Locals", ids["debug_local"])

    ui.place_grid_child(ids["debug_panel"], ids["source"], 0, 0)
    ui.place_grid_child(ids["debug_panel"], ids["debug_tabs"], 1, 0)
    ui.place_grid_child(ids["container"], ids["debug_panel"], 0, 0)
    ui.add_tab("editor.tabs", title, ids["container"],
               button_glyph="×", button_action=f"tab.close.{tab_id}")


def _build_kernel_row_widgets(ui: UiDocumentBuilder, tab_id: str, kernel_name: str):
    row_id = f"nav.debug.kernel.{tab_id}"
    ui.create_grid(row_id)
    ui.add_grid_column_exact(row_id, 6)    # col 0: left indent
    ui.add_grid_column_fill(row_id)        # col 1: name (row 0) / worker combo (row 1)
    ui.add_grid_column_exact(row_id, 20)   # col 2: load indicator
    ui.add_grid_column_exact(row_id, 20)   # col 3: run indicator
    ui.add_grid_column_exact(row_id, 12)   # col 4: spacer before queue
    ui.add_grid_column_exact(row_id, 72)   # col 5: queue badge "total / worker"
    ui.add_grid_column_exact(row_id, 48)   # col 6: debug checkbox (row 0)
    ui.add_grid_column_exact(row_id, 32)   # col 7: ▶  (row 1)
    ui.add_grid_column_exact(row_id, 32)   # col 8: ▶| (row 1)
    ui.add_grid_column_exact(row_id, 4)    # col 9: right margin
    ui.add_grid_row_exact(row_id, 26)      # row 0: name + queue badge
    ui.add_grid_row_exact(row_id, 26)      # row 1: worker combo + buttons
    for dot_name in ("load_ind", "run_ind"):
        dot_id = f"{row_id}.{dot_name}"
        ui.create_widget(dot_id, "demo.widgets.status_dot")
        ui.set_property_string(dot_id, "foreground_color", "#666666")
    ui.create_label(f"{row_id}.name", kernel_name, 12)
    ui.create_label(f"{row_id}.queue", "0 / 0", 10)
    ui.set_property_bool(f"{row_id}.queue", "enabled", False)
    ui.create_combo_box(f"{row_id}.worker", items=[], selected_id="")
    ui.create_checkbox(f"{row_id}.debug", "Dbg", True)
    ui.set_property_number(f"{row_id}.debug", "font_size", 10)
    ui.create_button(f"{row_id}.run",  "▶",  f"debug.kernel.run.{tab_id}")
    ui.set_property_number(f"{row_id}.run",  "font_size", 11)
    ui.create_button(f"{row_id}.step", "▶|", f"debug.kernel.step.{tab_id}")
    ui.set_property_number(f"{row_id}.step", "font_size", 11)
    ui.place_grid_child(row_id, f"{row_id}.load_ind", 2, 0)
    ui.place_grid_child(row_id, f"{row_id}.run_ind",  3, 0)
    # row 0: name + queue + debug checkbox
    ui.place_grid_child(row_id, f"{row_id}.name",   1, 0)
    ui.place_grid_child(row_id, f"{row_id}.queue",  5, 0)
    ui.place_grid_child(row_id, f"{row_id}.debug",  6, 0)
    # row 1: worker combo + buttons
    ui.place_grid_child(row_id, f"{row_id}.worker", 1, 1, 3, 1)
    ui.place_grid_child(row_id, f"{row_id}.run",    7, 1)
    ui.place_grid_child(row_id, f"{row_id}.step",   8, 1)

import json
import subprocess
import sys
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder


def build_open_file_dialog(initial_path: str):
    """Build a high-quality open-file dialog using the current Elara UI API.

    Current UI API limitations are represented as deliberate placeholders rather
    than fake controls:
    - no table/header/list columns yet, so the file browser is a rich list label
    - no combo box yet, so file type is represented by a text input
    - no checkbox yet in this dialog flow, so option toggles are label placeholders
    - no modal default/escape button metadata yet, so actions are plain buttons
    """
    import app as _app_module
    _breadcrumb_for = _app_module._breadcrumb_for
    _open_file_items = _app_module._open_file_items
    ui = UiDocumentBuilder()
    ui.create_window("Open File", 920, 640, "org.elara.ui.epa-ide.open-file")
    ui.set_theme_mode("dark")

    # Overall dialog:
    #   left places rail | main browser | right preview/details
    #   bottom file-name/type/action strip spans all columns.
    ui.create_grid("dialog.shell")
    ui.add_grid_column_exact("dialog.shell", 190)
    ui.add_grid_column_weighted_fill("dialog.shell", 4)
    ui.add_grid_column_exact("dialog.shell", 230)
    ui.set_grid_column_border_resizable("dialog.shell", 0, True)
    ui.set_grid_column_border_resizable("dialog.shell", 1, True)
    ui.add_grid_row_fill("dialog.shell")
    ui.add_grid_row_exact("dialog.shell", 112)
    ui.set_root_content("dialog.shell")

    # Places/sidebar.
    ui.create_grid("dialog.places_panel")
    ui.add_grid_column_fill("dialog.places_panel")
    ui.add_grid_row_exact("dialog.places_panel", 28)
    ui.add_grid_row_fill("dialog.places_panel")
    ui.add_grid_row_exact("dialog.places_panel", 44)
    ui.create_label("dialog.places_title", "Places", 14)
    ui.create_tree_view("dialog.places")
    ui.set_property_number("dialog.places", "font_size", 14)
    ui.set_section_json("dialog.places", "items", [
        {"id": "place.recent", "label": "Recent", "expanded": True, "children": [
            {"id": "place.recent.main", "label": "main.e"},
            {"id": "place.recent.runtime", "label": "runtime_host.cpp"},
            {"id": "place.recent.agent", "label": "agent_console.py"}
        ]},
        {"id": "place.home", "label": "Home", "expanded": True, "children": [
            {"id": "place.home.projects", "label": "Projects"},
            {"id": "place.home.documents", "label": "Documents"},
            {"id": "place.home.downloads", "label": "Downloads"}
        ]},
        {"id": "place.workspace", "label": "Workspace", "expanded": True, "children": [
            {"id": "place.workspace.e", "label": "E"},
            {"id": "place.workspace.cpp", "label": "C++"},
            {"id": "place.workspace.python", "label": "Python"}
        ]},
        {"id": "place.computer", "label": "Computer"},
        {"id": "place.network", "label": "Network"}
    ])
    ui.create_label("dialog.places_note", "Placeholder: no native volumes / bookmarks API yet", 11)
    ui.place_grid_child("dialog.places_panel", "dialog.places_title", 0, 0)
    ui.place_grid_child("dialog.places_panel", "dialog.places", 0, 1)
    ui.place_grid_child("dialog.places_panel", "dialog.places_note", 0, 2)

    # Main browser panel with location controls, file list, and status row.
    ui.create_grid("dialog.browser_panel")
    ui.add_grid_column_fill("dialog.browser_panel")
    ui.add_grid_row_exact("dialog.browser_panel", 42)
    ui.add_grid_row_exact("dialog.browser_panel", 38)
    ui.add_grid_row_exact("dialog.browser_panel", 24)
    ui.add_grid_row_fill("dialog.browser_panel")
    ui.add_grid_row_exact("dialog.browser_panel", 28)

    ui.create_grid("dialog.navbar")
    ui.add_grid_column_exact("dialog.navbar", 36)
    ui.add_grid_column_exact("dialog.navbar", 36)
    ui.add_grid_column_exact("dialog.navbar", 36)
    ui.add_grid_column_fill("dialog.navbar")
    ui.add_grid_column_exact("dialog.navbar", 170)
    ui.add_grid_row_fill("dialog.navbar")
    ui.create_button("dialog.nav.back", "‹", "dialog.nav.back")
    ui.create_button("dialog.nav.forward", "›", "dialog.nav.forward")
    ui.create_button("dialog.nav.up", "↑", "dialog.nav.up")
    ui.create_text_input("dialog.location", "Location", initial_path)
    ui.create_text_input("dialog.search", "Search current folder", "")
    ui.place_grid_child("dialog.navbar", "dialog.nav.back", 0, 0)
    ui.place_grid_child("dialog.navbar", "dialog.nav.forward", 1, 0)
    ui.place_grid_child("dialog.navbar", "dialog.nav.up", 2, 0)
    ui.place_grid_child("dialog.navbar", "dialog.location", 3, 0)
    ui.place_grid_child("dialog.navbar", "dialog.search", 4, 0)

    ui.create_grid("dialog.breadcrumb_bar")
    ui.add_grid_column_fill("dialog.breadcrumb_bar")
    ui.add_grid_column_exact("dialog.breadcrumb_bar", 88)
    ui.add_grid_column_exact("dialog.breadcrumb_bar", 88)
    ui.add_grid_row_fill("dialog.breadcrumb_bar")
    ui.create_label("dialog.breadcrumb", _breadcrumb_for(initial_path), 13)
    ui.create_button("dialog.new_folder", "New Folder", "dialog.folder.new")
    ui.create_button("dialog.refresh", "Refresh", "dialog.folder.refresh")
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.breadcrumb", 0, 0)
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.new_folder", 1, 0)
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.refresh", 2, 0)

    ui.create_list_view("dialog.files")
    ui.set_property_number("dialog.files", "font_size", 14)
    ui.set_section_json("dialog.files", "items", _open_file_items(initial_path))

    ui.create_label("dialog.file_header", "Name", 12)
    _initial_items = _open_file_items(initial_path)
    ui.create_label("dialog.file_status", f"{len(_initial_items)} items", 11)
    ui.place_grid_child("dialog.browser_panel", "dialog.navbar", 0, 0)
    ui.place_grid_child("dialog.browser_panel", "dialog.breadcrumb_bar", 0, 1)
    ui.place_grid_child("dialog.browser_panel", "dialog.file_header", 0, 2)
    ui.place_grid_child("dialog.browser_panel", "dialog.files", 0, 3)
    ui.place_grid_child("dialog.browser_panel", "dialog.file_status", 0, 4)

    # Preview/details pane.
    ui.create_grid("dialog.preview_panel")
    ui.add_grid_column_fill("dialog.preview_panel")
    ui.add_grid_row_exact("dialog.preview_panel", 28)
    ui.add_grid_row_exact("dialog.preview_panel", 110)
    ui.add_grid_row_exact("dialog.preview_panel", 28)
    ui.add_grid_row_fill("dialog.preview_panel")
    ui.add_grid_row_exact("dialog.preview_panel", 54)
    ui.create_label("dialog.preview_title", "Preview", 14)
    ui.create_rich_text_edit("dialog.preview_text", "# main.e\n\nfn main() {\n    print(\"EPA IDE prototype\");\n}\n")
    ui.set_property_number("dialog.preview_text", "font_size", 12)
    ui.create_label("dialog.details_title", "Details", 14)
    ui.create_list_view("dialog.details")
    ui.set_property_number("dialog.details", "font_size", 12)
    ui.set_section_json("dialog.details", "items", [
        {"id": "detail.name", "label": "Name: main.e"},
        {"id": "detail.type", "label": "Type: E source"},
        {"id": "detail.size", "label": "Size: 4 KB"},
        {"id": "detail.modified", "label": "Modified: Today"},
        {"id": "detail.path", "label": "Path: /home/user/Projects/E/main.e"}
    ])
    ui.create_label("dialog.preview_note", "Placeholder: preview is static until selection events update this pane", 11)
    ui.place_grid_child("dialog.preview_panel", "dialog.preview_title", 0, 0)
    ui.place_grid_child("dialog.preview_panel", "dialog.preview_text", 0, 1)
    ui.place_grid_child("dialog.preview_panel", "dialog.details_title", 0, 2)
    ui.place_grid_child("dialog.preview_panel", "dialog.details", 0, 3)
    ui.place_grid_child("dialog.preview_panel", "dialog.preview_note", 0, 4)

    # Bottom strip: file name, file type, options, and action buttons.
    ui.create_grid("dialog.bottom")
    ui.add_grid_column_exact("dialog.bottom", 92)
    ui.add_grid_column_weighted_fill("dialog.bottom", 3)
    ui.add_grid_column_exact("dialog.bottom", 82)
    ui.add_grid_column_exact("dialog.bottom", 180)
    ui.add_grid_column_exact("dialog.bottom", 100)
    ui.add_grid_column_exact("dialog.bottom", 100)
    ui.add_grid_row_exact("dialog.bottom", 38)
    ui.add_grid_row_exact("dialog.bottom", 34)
    ui.add_grid_row_exact("dialog.bottom", 34)

    ui.create_label("dialog.filename_label", "File name:", 13)
    ui.create_text_input("dialog.filename", "File name", "main.e")
    ui.create_label("dialog.filetype_label", "File type:", 13)
    ui.create_text_input("dialog.filetype", "File type filter", "All supported files (*.e *.cpp *.h *.py *.eproj)")
    ui.create_button("dialog.cancel_button", "Cancel", "dialog.file.cancel")
    ui.create_button("dialog.open_button", "Open", "dialog.file.confirm")
    ui.create_label("dialog.options_label", "Options:", 13)
    ui.create_label("dialog.options", "☐ Read only    ☐ Show hidden files    Placeholder: checkbox/combo-box dialog metadata pending", 11)
    ui.create_label("dialog.default_action_note", "Open = default action, Esc = cancel once keyboard/dialog policy is exposed by the UI head", 11)

    ui.place_grid_child("dialog.bottom", "dialog.filename_label", 0, 0)
    ui.place_grid_child("dialog.bottom", "dialog.filename", 1, 0)
    ui.place_grid_child("dialog.bottom", "dialog.filetype_label", 2, 0)
    ui.place_grid_child("dialog.bottom", "dialog.filetype", 3, 0)
    ui.place_grid_child("dialog.bottom", "dialog.cancel_button", 4, 0)
    ui.place_grid_child("dialog.bottom", "dialog.open_button", 5, 0)
    ui.place_grid_child("dialog.bottom", "dialog.options_label", 0, 1)
    ui.place_grid_child("dialog.bottom", "dialog.options", 1, 1, 5, 1)
    ui.place_grid_child("dialog.bottom", "dialog.default_action_note", 1, 2, 5, 1)

    ui.place_grid_child("dialog.shell", "dialog.places_panel", 0, 0)
    ui.place_grid_child("dialog.shell", "dialog.browser_panel", 1, 0)
    ui.place_grid_child("dialog.shell", "dialog.preview_panel", 2, 0)
    ui.place_grid_child("dialog.shell", "dialog.bottom", 0, 1, 3, 1)
    return ui

def build_new_project_wizard(initial_path: str):
    ui = UiDocumentBuilder()
    ui.create_window("New Project", 540, 760, "org.elara.ui.epa-ide.new-project")
    ui.set_theme_mode("dark")

    # Outer shell: left-pad(20) | content(fill) | right-pad(20)
    ui.create_grid("wizard.shell")
    ui.add_grid_column_exact("wizard.shell", 20)
    ui.add_grid_column_weighted_fill("wizard.shell", 1)
    ui.add_grid_column_exact("wizard.shell", 20)
    ui.add_grid_row_exact("wizard.shell", 20)   # 0 top pad
    ui.add_grid_row_exact("wizard.shell", 30)   # 1 title
    ui.add_grid_row_exact("wizard.shell", 14)   # 2 gap
    ui.add_grid_row_exact("wizard.shell", 36)   # 3 name row
    ui.add_grid_row_exact("wizard.shell", 16)   # 4 gap
    ui.add_grid_row_exact("wizard.shell", 20)   # 5 tech label
    ui.add_grid_row_exact("wizard.shell", 28)   # 6 epa
    ui.add_grid_row_exact("wizard.shell", 28)   # 7 cpp
    ui.add_grid_row_exact("wizard.shell", 28)   # 8 python
    ui.add_grid_row_exact("wizard.shell", 16)   # 9 gap
    ui.add_grid_row_exact("wizard.shell", 20)   # 10 scaffold label
    ui.add_grid_row_exact("wizard.shell", 34)   # 11 ui client row
    ui.add_grid_row_exact("wizard.shell", 34)   # 12 ui template row
    ui.add_grid_row_exact("wizard.shell", 28)   # 13 multi cpu row
    ui.add_grid_row_exact("wizard.shell", 28)   # 14 epa vm host row
    ui.add_grid_row_exact("wizard.shell", 28)   # 15 epa debug rpc row
    ui.add_grid_row_exact("wizard.shell", 34)   # 16 rpc host row
    ui.add_grid_row_exact("wizard.shell", 34)   # 17 rpc port row
    ui.add_grid_row_exact("wizard.shell", 16)   # 18 gap
    ui.add_grid_row_exact("wizard.shell", 20)   # 19 location label
    ui.add_grid_row_exact("wizard.shell", 34)   # 20 path bar
    ui.add_grid_row_fill("wizard.shell")        # 21 folder list
    ui.add_grid_row_exact("wizard.shell", 24)   # 22 error/validation
    ui.add_grid_row_exact("wizard.shell", 52)   # 23 footer
    ui.set_root_content("wizard.shell")

    # ── Title ──────────────────────────────────────────────────────────
    ui.create_label("wizard.title", "New Project", 15)
    ui.place_grid_child("wizard.shell", "wizard.title", 1, 1)

    # ── Project name row ───────────────────────────────────────────────
    ui.create_grid("wizard.name_row")
    ui.add_grid_column_exact("wizard.name_row", 110)
    ui.add_grid_column_weighted_fill("wizard.name_row", 1)
    ui.add_grid_row_fill("wizard.name_row")
    ui.create_label("wizard.name_label", "Project name:", 13)
    ui.create_text_input("wizard.project_name", "my-project")
    ui.place_grid_child("wizard.name_row", "wizard.name_label", 0, 0)
    ui.place_grid_child("wizard.name_row", "wizard.project_name", 1, 0)
    ui.place_grid_child("wizard.shell", "wizard.name_row", 1, 3)

    # ── Technologies ───────────────────────────────────────────────────
    ui.create_label("wizard.tech_label", "Technologies (select at least one):", 13)
    ui.place_grid_child("wizard.shell", "wizard.tech_label", 1, 5)

    ui.create_checkbox("wizard.tech.epa", "EPA", True)
    ui.create_checkbox("wizard.tech.cpp", "C++", True)
    ui.create_checkbox("wizard.tech.python", "Python", True)
    ui.place_grid_child("wizard.shell", "wizard.tech.epa", 1, 6)
    ui.place_grid_child("wizard.shell", "wizard.tech.cpp", 1, 7)
    ui.place_grid_child("wizard.shell", "wizard.tech.python", 1, 8)

    # ── UI scaffold options ────────────────────────────────────────────
    ui.create_label("wizard.scaffold_label", "UI App Scaffold:", 13)
    ui.place_grid_child("wizard.shell", "wizard.scaffold_label", 1, 10)

    ui.create_grid("wizard.ui_client_row")
    ui.add_grid_column_exact("wizard.ui_client_row", 140)
    ui.add_grid_column_weighted_fill("wizard.ui_client_row", 1)
    ui.add_grid_row_fill("wizard.ui_client_row")
    ui.create_label("wizard.ui_client_label", "UI client language:", 13)
    ui.create_combo_box(
        "wizard.ui_client",
        items=[
            {"id": "both", "label": "C++ and Python"},
            {"id": "cpp", "label": "C++ only"},
            {"id": "python", "label": "Python only"},
        ],
        selected_id="both",
    )
    ui.place_grid_child("wizard.ui_client_row", "wizard.ui_client_label", 0, 0)
    ui.place_grid_child("wizard.ui_client_row", "wizard.ui_client", 1, 0)
    ui.place_grid_child("wizard.shell", "wizard.ui_client_row", 1, 11)

    ui.create_grid("wizard.ui_template_row")
    ui.add_grid_column_exact("wizard.ui_template_row", 140)
    ui.add_grid_column_weighted_fill("wizard.ui_template_row", 1)
    ui.add_grid_row_fill("wizard.ui_template_row")
    ui.create_label("wizard.ui_template_label", "UI template:", 13)
    ui.create_combo_box(
        "wizard.ui_template",
        items=[
            {"id": "tabbed-control-panel", "label": "Tabbed Control Panel"},
            {"id": "rich-editor", "label": "Rich Editor"},
        ],
        selected_id="tabbed-control-panel",
    )
    ui.place_grid_child("wizard.ui_template_row", "wizard.ui_template_label", 0, 0)
    ui.place_grid_child("wizard.ui_template_row", "wizard.ui_template", 1, 0)
    ui.place_grid_child("wizard.shell", "wizard.ui_template_row", 1, 12)

    ui.create_checkbox("wizard.python.multi_cpu", "Python multi-core worker template", False)
    ui.place_grid_child("wizard.shell", "wizard.python.multi_cpu", 1, 13)

    ui.create_checkbox("wizard.cpp.epa_vm_host", "EPA VM Host adapter (C++ UI only)", False)
    ui.place_grid_child("wizard.shell", "wizard.cpp.epa_vm_host", 1, 14)

    ui.create_checkbox("wizard.cpp.epa_debug_rpc", "EPA debug JSON-RPC target", True)
    ui.place_grid_child("wizard.shell", "wizard.cpp.epa_debug_rpc", 1, 15)

    ui.create_grid("wizard.rpc_host_row")
    ui.add_grid_column_exact("wizard.rpc_host_row", 140)
    ui.add_grid_column_weighted_fill("wizard.rpc_host_row", 1)
    ui.add_grid_row_fill("wizard.rpc_host_row")
    ui.create_label("wizard.rpc_host_label", "UI RPC host:", 13)
    ui.create_text_input("wizard.rpc_host", "127.0.0.1")
    ui.place_grid_child("wizard.rpc_host_row", "wizard.rpc_host_label", 0, 0)
    ui.place_grid_child("wizard.rpc_host_row", "wizard.rpc_host", 1, 0)
    ui.place_grid_child("wizard.shell", "wizard.rpc_host_row", 1, 16)

    ui.create_grid("wizard.rpc_port_row")
    ui.add_grid_column_exact("wizard.rpc_port_row", 140)
    ui.add_grid_column_weighted_fill("wizard.rpc_port_row", 1)
    ui.add_grid_row_fill("wizard.rpc_port_row")
    ui.create_label("wizard.rpc_port_label", "UI RPC port:", 13)
    ui.create_text_input("wizard.rpc_port", "18777")
    ui.place_grid_child("wizard.rpc_port_row", "wizard.rpc_port_label", 0, 0)
    ui.place_grid_child("wizard.rpc_port_row", "wizard.rpc_port", 1, 0)
    ui.place_grid_child("wizard.shell", "wizard.rpc_port_row", 1, 17)

    # ── Save location ──────────────────────────────────────────────────
    ui.create_label("wizard.loc_label", "Save location:", 13)
    ui.place_grid_child("wizard.shell", "wizard.loc_label", 1, 19)

    # Path bar: current-path label (fill) + Up button (36px)
    ui.create_grid("wizard.path_bar")
    ui.add_grid_column_weighted_fill("wizard.path_bar", 1)
    ui.add_grid_column_exact("wizard.path_bar", 36)
    ui.add_grid_column_exact("wizard.path_bar", 36)
    ui.add_grid_row_fill("wizard.path_bar")
    ui.create_label("wizard.path_display", initial_path, 12)
    ui.create_button("wizard.nav.home", "⌂", "wizard.nav.home")
    ui.create_button("wizard.nav.up", "↑", "wizard.nav.up")
    ui.place_grid_child("wizard.path_bar", "wizard.path_display", 0, 0)
    ui.place_grid_child("wizard.path_bar", "wizard.nav.home", 1, 0)
    ui.place_grid_child("wizard.path_bar", "wizard.nav.up", 2, 0)
    ui.place_grid_child("wizard.shell", "wizard.path_bar", 1, 20)

    # Folder list — populated with subdirs of initial_path
    ui.create_list_view("wizard.folder_list")
    ui.set_property_number("wizard.folder_list", "font_size", 13)
    ui.set_section_json("wizard.folder_list", "items",
                        _folder_items(initial_path))
    ui.place_grid_child("wizard.shell", "wizard.folder_list", 1, 21)

    # ── Validation message ─────────────────────────────────────────────
    ui.create_label("wizard.error", "", 12)
    ui.place_grid_child("wizard.shell", "wizard.error", 1, 22)

    # ── Footer ─────────────────────────────────────────────────────────
    ui.create_grid("wizard.footer")
    ui.add_grid_column_weighted_fill("wizard.footer", 1)
    ui.add_grid_column_exact("wizard.footer", 88)
    ui.add_grid_column_exact("wizard.footer", 10)
    ui.add_grid_column_exact("wizard.footer", 100)
    ui.add_grid_row_fill("wizard.footer")
    ui.create_button("wizard.cancel_btn", "Cancel", "wizard.cancel")
    ui.create_button("wizard.create_btn", "Create Project", "wizard.create")
    ui.place_grid_child("wizard.footer", "wizard.cancel_btn", 1, 0)
    ui.place_grid_child("wizard.footer", "wizard.create_btn", 3, 0)
    ui.place_grid_child("wizard.shell", "wizard.footer", 1, 23)

    return ui


def build_open_project_dialog(initial_path: str):
    ui = UiDocumentBuilder()
    ui.create_window("Open Project", 500, 500, "org.elara.ui.epa-ide.open-project")
    ui.set_theme_mode("dark")

    ui.create_grid("open_project.shell")
    ui.add_grid_column_exact("open_project.shell", 20)
    ui.add_grid_column_weighted_fill("open_project.shell", 1)
    ui.add_grid_column_exact("open_project.shell", 20)
    ui.add_grid_row_exact("open_project.shell", 20)  # 0 top pad
    ui.add_grid_row_exact("open_project.shell", 30)  # 1 title
    ui.add_grid_row_exact("open_project.shell", 14)  # 2 gap
    ui.add_grid_row_exact("open_project.shell", 34)  # 3 path bar
    ui.add_grid_row_fill("open_project.shell")       # 4 folder list
    ui.add_grid_row_exact("open_project.shell", 24)  # 5 hint
    ui.add_grid_row_exact("open_project.shell", 52)  # 6 footer
    ui.set_root_content("open_project.shell")

    ui.create_label("open_project.title", "Open Project", 15)
    ui.place_grid_child("open_project.shell", "open_project.title", 1, 1)

    ui.create_grid("open_project.path_bar")
    ui.add_grid_column_weighted_fill("open_project.path_bar", 1)
    ui.add_grid_column_exact("open_project.path_bar", 36)
    ui.add_grid_column_exact("open_project.path_bar", 36)
    ui.add_grid_row_fill("open_project.path_bar")
    ui.create_label("open_project.path_display", initial_path, 12)
    ui.create_button("open_project.nav.home", "⌂", "open_project.nav.home")
    ui.create_button("open_project.nav.up", "↑", "open_project.nav.up")
    ui.place_grid_child("open_project.path_bar", "open_project.path_display", 0, 0)
    ui.place_grid_child("open_project.path_bar", "open_project.nav.home", 1, 0)
    ui.place_grid_child("open_project.path_bar", "open_project.nav.up", 2, 0)
    ui.place_grid_child("open_project.shell", "open_project.path_bar", 1, 3)

    ui.create_list_view("open_project.folder_list")
    ui.set_property_number("open_project.folder_list", "font_size", 13)
    ui.set_section_json("open_project.folder_list", "items", _folder_items(initial_path))
    ui.place_grid_child("open_project.shell", "open_project.folder_list", 1, 4)

    ui.create_label("open_project.hint", "Double-click a folder to navigate or open as project", 11)
    ui.place_grid_child("open_project.shell", "open_project.hint", 1, 5)

    ui.create_grid("open_project.footer")
    ui.add_grid_column_weighted_fill("open_project.footer", 1)
    ui.add_grid_column_exact("open_project.footer", 66)
    ui.add_grid_column_exact("open_project.footer", 10)
    ui.add_grid_column_exact("open_project.footer", 120)
    ui.add_grid_row_fill("open_project.footer")
    ui.create_button("open_project.cancel_btn", "Cancel", "open_project.cancel")
    ui.create_button("open_project.open_btn", "Open Project", "open_project.open")
    ui.place_grid_child("open_project.footer", "open_project.cancel_btn", 1, 0)
    ui.place_grid_child("open_project.footer", "open_project.open_btn", 3, 0)
    ui.place_grid_child("open_project.shell", "open_project.footer", 1, 6)

    return ui


def _to_class_name(stem: str) -> str:
    parts = []
    current = []
    for ch in stem:
        if ch in ('_', '-', ' '):
            if current:
                parts.append(''.join(current))
                current = []
        else:
            current.append(ch)
    if current:
        parts.append(''.join(current))
    return ''.join(p.capitalize() for p in parts if p) or stem.capitalize()


def _to_symbol_name(stem: str) -> str:
    chars = []
    last_was_sep = False
    for ch in stem:
        if ch.isalnum():
            chars.append(ch.lower())
            last_was_sep = False
        elif not last_was_sep:
            chars.append('_')
            last_was_sep = True
    symbol = ''.join(chars).strip('_')
    if not symbol:
        return "module"
    if symbol[0].isdigit():
        symbol = f"e_{symbol}"
    return symbol


def _cpp_header_content(header_name: str) -> str:
    stem = Path(header_name).stem
    cls = _to_class_name(stem)
    return (
        "#pragma once\n\n"
        f"class {cls} {{\n"
        "public:\n"
        f"    {cls}();\n"
        f"    ~{cls}();\n"
        "};\n"
    )


def _cpp_source_content(source_name: str) -> str:
    stem = Path(source_name).stem
    cls = _to_class_name(stem)
    header_name = f"{stem}.h"
    return (
        f'#include "{header_name}"\n\n'
        f'{cls}::{cls}() {{\n'
        "}\n\n"
        f'{cls}::~{cls}() {{\n'
        "}\n"
    )


E_FILE_TEMPLATES = {
    "root_node": {
        "label": "Root Node",
        "summary": "Top-level kernel coordinator with a primary worker and a child-kernel ingress worker.",
    },
    "specialised_worker": {
        "label": "Specialised Worker",
        "summary": "Focused worker with functions, local arena payload staging, kernel/host/far signal split, and loop logic.",
    },
    "child_kernel_router": {
        "label": "Child Kernel Router",
        "summary": "Union ingress worker for EPABlob and KeyInput routing into a child kernel.",
    },
    "pipeline_chain": {
        "label": "Pipeline Chain",
        "summary": "Multi-worker handoff example using @attributes and next for pipeline composition.",
    },
    "feature_showcase": {
        "label": "Feature Showcase",
        "summary": "Broad current-state E sample covering locals, functions, raw EPA, loops, branching, and signaling.",
    },
}


def _e_template_items():
    return [{"id": key, "label": meta["label"]} for key, meta in E_FILE_TEMPLATES.items()]


def _e_template_summary(template_id: str) -> str:
    meta = E_FILE_TEMPLATES.get(template_id) or E_FILE_TEMPLATES["root_node"]
    return f"{meta['label']}\n\n{meta['summary']}"


def _e_root_node_template(file_name: str) -> str:
    stem = Path(file_name).stem
    kernel_uid = f"todo.{_to_symbol_name(stem)}"
    type_name = f"{_to_class_name(stem)}Payload"
    worker_name = f"{_to_symbol_name(stem)}_worker"
    child_worker_name = f"{_to_symbol_name(stem)}_child_kernel_worker"
    payload_name = "payload"
    return (
        "declare default_in_words 256\n"
        "declare default_out_words 256\n"
        "declare default_signal_mail_box_size 128\n"
        "\n"
        f"struct {type_name};\n"
        "struct EPABlob;\n"
        "struct KeyInput;\n"
        "\n"
        f"type {type_name}(int tag) {{\n"
        "  return tag;\n"
        "}\n"
        "\n"
        "type EPABlob(int blob_id) {\n"
        "  return blob_id;\n"
        "}\n"
        "\n"
        "type KeyInput(int key_code) {\n"
        "  return key_code;\n"
        "}\n"
        "\n"
        "kernel(VM vm) {\n"
        f"  kernalId(\"{kernel_uid}\");\n"
        f"  {worker_name}(vm);\n"
        f"  {child_worker_name}(vm);\n"
        "  int wid = 0;\n"
        "  while (wid = kernel_wait_signal()) {\n"
        "    if (wid == 1) {\n"
        f"      {type_name} {payload_name} = kernal_get_ghs(1);\n"
        "      // TODO: integrate the worker payload into kernel context/state here.\n"
        "    } else if (wid == 2) {\n"
        "      // TODO: integrate child-kernel coordination or artifact state here.\n"
        "    } else {\n"
        "      // TODO: handle additional worker signals here.\n"
        "    }\n"
        "  }\n"
        "}\n"
        "\n"
        "// acl {\n"
        "//   \"todo.remote.kernel\" -> "
        f"{worker_name};\n"
        "// }\n"
        "\n"
        f"worker {worker_name}({type_name} {payload_name}) {{\n"
        "  // TODO: add worker logic here.\n"
        "  // TODO: call kernel_signal() after updating the worker payload for kernel integration.\n"
        "  kernel_signal();\n"
        "}\n"
        "\n"
        f"worker {child_worker_name}(EPABlob|KeyInput ingress) {{\n"
        "  int ingress_kind = typeof(ingress);\n"
        "  if (ingress_kind == typeid(EPABlob)) {\n"
        "    // TODO: load or refresh a child kernel from the incoming EPA blob here.\n"
        "  } else if (ingress_kind == typeid(KeyInput)) {\n"
        "    // TODO: forward key input into the running child kernel here.\n"
        "  } else {\n"
        "    // TODO: handle additional child-kernel ingress payload types here.\n"
        "  }\n"
        "  // TODO: publish any child-kernel artifact needed by the parent kernel before kernel_signal().\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_specialised_worker_template(file_name: str) -> str:
    stem = Path(file_name).stem
    kernel_uid = f"todo.{_to_symbol_name(stem)}"
    type_name = f"{_to_class_name(stem)}Payload"
    route_type = f"{_to_class_name(stem)}Route"
    worker_name = f"{_to_symbol_name(stem)}_worker"
    return (
        "declare default_in_words 256\n"
        "declare default_out_words 256\n"
        "declare default_signal_mail_box_size 128\n"
        "\n"
        f"struct {type_name};\n"
        f"struct {route_type};\n"
        "\n"
        f"type {type_name}(int opcode, int counter) {{\n"
        "  return opcode;\n"
        "}\n"
        "\n"
        f"type {route_type}(int lane) {{\n"
        "  return lane;\n"
        "}\n"
        "\n"
        "kernel(VM vm) {\n"
        f"  kernalId(\"{kernel_uid}\");\n"
        f"  {worker_name}(vm);\n"
        "  int wid = 0;\n"
        "  while (wid = kernel_wait_signal()) {\n"
        "    if (wid == 1) {\n"
        f"      {type_name} payload = kernal_get_ghs(1);\n"
        "      // TODO: consume the specialised worker update here.\n"
        "    }\n"
        "  }\n"
        "}\n"
        "\n"
        "// acl {\n"
        f"//   \"todo.remote.kernel\" -> {worker_name};\n"
        "// }\n"
        "\n"
        "function int clamp_count(int count) {\n"
        "  int result = count;\n"
        "  if (result == 0) {\n"
        "    result = 1;\n"
        "  } else if (result == 1) {\n"
        "    result = result + 1;\n"
        "  } else {\n"
        "    result = result + 2;\n"
        "  }\n"
        "  return result;\n"
        "}\n"
        "\n"
        f"worker {worker_name}({type_name} payload) {{\n"
        "  reg int loop_count;\n"
        f"  local {route_type} outbound;\n"
        "  int count = clamp_count(payload.counter);\n"
        "  loop_count = count;\n"
        "  while (loop_count) {\n"
        "    loop_count = loop_count - 1;\n"
        "  }\n"
        "  // TODO: replace the placeholder string with the real remote kernel id.\n"
        "  // TODO: populate outbound from worker-local state before far_signal().\n"
        "  far_signal(\"todo.remote.kernel\", outbound);\n"
        "  host_signal();\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_child_kernel_router_template(file_name: str) -> str:
    stem = Path(file_name).stem
    kernel_uid = f"todo.{_to_symbol_name(stem)}"
    root_type = f"{_to_class_name(stem)}Root"
    router_name = f"{_to_symbol_name(stem)}_router"
    return (
        "declare default_in_words 256\n"
        "declare default_out_words 256\n"
        "declare default_signal_mail_box_size 128\n"
        "\n"
        f"struct {root_type};\n"
        "struct EPABlob;\n"
        "struct KeyInput;\n"
        "\n"
        f"type {root_type}(int scene_id) {{\n"
        "  return scene_id;\n"
        "}\n"
        "\n"
        "type EPABlob(int blob_id) {\n"
        "  return blob_id;\n"
        "}\n"
        "\n"
        "type KeyInput(int key_code) {\n"
        "  return key_code;\n"
        "}\n"
        "\n"
        "kernel(VM vm) {\n"
        f"  kernalId(\"{kernel_uid}\");\n"
        f"  {router_name}(vm);\n"
        "  int wid = 0;\n"
        "  while (wid = kernel_wait_signal()) {\n"
        "    if (wid == 1) {\n"
        "      // TODO: merge the routed child-kernel result into root state here.\n"
        "      // TODO: use a dedicated typed worker signal path once the child kernel publishes a stable result type.\n"
        "    }\n"
        "  }\n"
        "}\n"
        "\n"
        "// acl {\n"
        f"//   \"todo.remote.kernel\" -> {router_name};\n"
        "// }\n"
        "\n"
        f"worker {router_name}(EPABlob|KeyInput ingress) {{\n"
        "  int ingress_kind = typeof(ingress);\n"
        "  if (ingress_kind == typeid(EPABlob)) {\n"
        "    // TODO: load or refresh a child kernel from the blob payload.\n"
        "  } else if (ingress_kind == typeid(KeyInput)) {\n"
        "    // TODO: route key input into the active child kernel.\n"
        "  } else {\n"
        "    // TODO: add more ingress variants here.\n"
        "  }\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_pipeline_chain_template(file_name: str) -> str:
    stem = Path(file_name).stem
    kernel_uid = f"todo.{_to_symbol_name(stem)}"
    type_name = f"{_to_class_name(stem)}Packet"
    base = _to_symbol_name(stem)
    return (
        "declare default_in_words 256\n"
        "declare default_out_words 256\n"
        "declare default_signal_mail_box_size 128\n"
        "\n"
        f"struct {type_name};\n"
        "\n"
        f"type {type_name}(int tag, int amount) {{\n"
        "  return tag;\n"
        "}\n"
        "\n"
        "kernel(VM vm) {\n"
        f"  kernalId(\"{kernel_uid}\");\n"
        f"  {base}_ingress(vm);\n"
        f"  {base}_transform(vm);\n"
        f"  {base}_egress(vm);\n"
        "  int wid = 0;\n"
        "  while (wid = kernel_wait_signal()) {\n"
        "    if (wid == 3) {\n"
        f"      {type_name} packet = kernal_get_ghs(3);\n"
        "      // TODO: consume completed pipeline output here.\n"
        "    }\n"
        "  }\n"
        "}\n"
        "\n"
        "// acl {\n"
        f"//   \"todo.remote.kernel\" -> {base}_ingress;\n"
        "// }\n"
        "\n"
        f"@attributes in_words:128 out_words:128 signal_mail_box_size:64\n"
        f"worker {base}_ingress({type_name} packet) {{\n"
        "  // TODO: validate or normalize ingress here.\n"
        f"  next {base}_transform;\n"
        "}\n"
        "\n"
        f"worker {base}_transform({type_name} packet) {{\n"
        "  // TODO: perform compute on packet here.\n"
        f"  next {base}_egress;\n"
        "}\n"
        "\n"
        f"@attributes in_words:64 out_words:64 signal_mail_box_size:32\n"
        f"worker {base}_egress({type_name} packet) {{\n"
        "  // TODO: prepare final packet state before returning to the kernel.\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_feature_showcase_template(file_name: str) -> str:
    stem = Path(file_name).stem
    kernel_uid = f"todo.{_to_symbol_name(stem)}"
    type_name = f"{_to_class_name(stem)}State"
    outbound_type = f"{_to_class_name(stem)}Event"
    worker_name = f"{_to_symbol_name(stem)}_worker"
    return (
        "declare default_in_words 256\n"
        "declare default_out_words 256\n"
        "declare default_signal_mail_box_size 128\n"
        "\n"
        f"struct {type_name};\n"
        f"struct {outbound_type};\n"
        "\n"
        f"type {type_name}(int mode, int count) {{\n"
        "  return mode;\n"
        "}\n"
        "\n"
        f"type {outbound_type}(int event_code) {{\n"
        "  return event_code;\n"
        "}\n"
        "\n"
        "kernel(VM vm) {\n"
        f"  kernalId(\"{kernel_uid}\");\n"
        f"  {worker_name}(vm);\n"
        "  int wid = 0;\n"
        "  while (wid = kernel_wait_signal()) {\n"
        "    if (wid == 1) {\n"
        f"      {type_name} state = kernal_get_ghs(1);\n"
        "      // TODO: integrate the showcase worker result here.\n"
        "    }\n"
        "  }\n"
        "}\n"
        "\n"
        "// acl {\n"
        f"//   \"todo.remote.kernel\" -> {worker_name};\n"
        "// }\n"
        "\n"
        "function int accumulate(int start) {\n"
        "  int sum = start;\n"
        "  for (int i = 3; i; i = i - 1) {\n"
        "    if (i == 2) {\n"
        "      continue;\n"
        "    }\n"
        "    sum = sum + i;\n"
        "  }\n"
        "  return sum;\n"
        "}\n"
        "\n"
        f"worker {worker_name}({type_name} state) {{\n"
        "  reg int loop_count;\n"
        f"  local {outbound_type} outbound;\n"
        "  int total = accumulate(state.count);\n"
        "  loop_count = total;\n"
        "  while (loop_count) {\n"
        "    if (loop_count == 2) {\n"
        "      loop_count = loop_count - 1;\n"
        "      continue;\n"
        "    } else if (loop_count == 1) {\n"
        "      break;\n"
        "    }\n"
        "    loop_count = loop_count - 1;\n"
        "  }\n"
        "  EPA {\n"
        "    // TODO: insert raw EPA instructions for fine-grained tuning here.\n"
        "  }\n"
        "  // TODO: replace the placeholder string with the real remote kernel id.\n"
        "  // TODO: fill outbound as a staged local-area message.\n"
        "  far_signal(\"todo.remote.kernel\", outbound);\n"
        "  host_signal();\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_file_content(file_name: str, template_id: str = "root_node") -> str:
    template_builders = {
        "root_node": _e_root_node_template,
        "specialised_worker": _e_specialised_worker_template,
        "child_kernel_router": _e_child_kernel_router_template,
        "pipeline_chain": _e_pipeline_chain_template,
        "feature_showcase": _e_feature_showcase_template,
    }
    builder = template_builders.get(template_id, _e_root_node_template)
    return builder(file_name)


def _artifact3d_template_content(file_name: str) -> str:
    name = Path(file_name).name
    for suffix in (".e3d.json", ".json"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    symbol = _to_symbol_name(name or "new_artifact")
    payload = {
        "format": "elara.model.template.v1",
        "name": symbol,
        "units": "meters",
        "metadata": {
            "description": "AI-editable primitive model template",
            "authoring_mode": "template",
            "compile_target": "mesh"
        },
        "materials": {
            "default": {
                "base_color": [1.0, 0.42, 0.08],
                "roughness": 0.65
            }
        },
        "parameters": {},
        "nodes": [
            {
                "id": "body",
                "primitive": "box",
                "position": [0.0, 0.5, 0.0],
                "size": [1.0, 1.0, 1.0],
                "material": "default"
            }
        ],
        "operations": [],
        "anchors": {
            "root": [0.0, 0.0, 0.0]
        },
        "compile": {
            "mesh_resolution": "medium",
            "generate_normals": True,
            "preserve_part_ids": True
        }
    }
    return json.dumps(payload, indent=2) + "\n"


def _file_content(tech: str, name: str, template_id: str | None = None) -> str:
    stem = Path(name).stem
    ext  = Path(name).suffix.lower()
    cls  = _to_class_name(stem)

    if ext == ".cpp":
        return _cpp_source_content(name)
    if ext == ".h":
        return _cpp_header_content(name)
    if ext == ".py":
        return (
            f'# {stem}\n\n\n'
            f'def main():\n    pass\n\n\n'
            f'if __name__ == "__main__":\n    main()\n'
        )
    if ext == ".e":
        return _e_file_content(name, template_id or "root_node")
    if tech == "Artifact3D" or name.endswith(".e3d.json"):
        return _artifact3d_template_content(name)
    return ""


def build_new_file_dialog(tech: str, initial_dir: str, selected_template: str | None = None):
    ph_map    = {"E": "my_module.e", "Cpp": "my_module.cpp", "Python": "my_module.py", "Artifact3D": "new_artifact.e3d.json"}
    label_map = {"E": "E", "Cpp": "C++", "Python": "Python", "Artifact3D": "3D Artifact Template"}
    label     = label_map.get(tech, tech)
    selected_template = selected_template or "root_node"

    ui = UiDocumentBuilder()
    ui.create_window(f"New {label} File", 460, 650 if tech == "E" else 520, "org.elara.ui.epa-ide.new-file")
    ui.set_theme_mode("dark")

    ui.create_grid("new_file.shell")
    ui.add_grid_column_exact("new_file.shell", 20)
    ui.add_grid_column_weighted_fill("new_file.shell", 1)
    ui.add_grid_column_exact("new_file.shell", 20)
    ui.add_grid_row_exact("new_file.shell", 20)   # 0 top pad
    ui.add_grid_row_exact("new_file.shell", 30)   # 1 title
    ui.add_grid_row_exact("new_file.shell", 14)   # 2 gap
    ui.add_grid_row_exact("new_file.shell", 36)   # 3 name row
    ui.add_grid_row_exact("new_file.shell", 12)   # 4 gap
    if tech == "E":
        ui.add_grid_row_exact("new_file.shell", 20)   # 5 template label
        ui.add_grid_row_exact("new_file.shell", 120)  # 6 template list
        ui.add_grid_row_exact("new_file.shell", 52)   # 7 template summary
        ui.add_grid_row_exact("new_file.shell", 12)   # 8 gap
        base_row = 9
    else:
        base_row = 5
    ui.add_grid_row_exact("new_file.shell", 20)   # base location label
    ui.add_grid_row_exact("new_file.shell", 34)   # base+1 path bar
    ui.add_grid_row_exact("new_file.shell", 34)   # base+2 new folder row
    ui.add_grid_row_fill("new_file.shell")        # base+3 folder list
    ui.add_grid_row_exact("new_file.shell", 24)   # base+4 error
    ui.add_grid_row_exact("new_file.shell", 52)   # base+5 footer
    ui.set_root_content("new_file.shell")

    ui.create_label("new_file.title", f"New {label} File", 15)
    ui.place_grid_child("new_file.shell", "new_file.title", 1, 1)

    ui.create_grid("new_file.name_row")
    ui.add_grid_column_exact("new_file.name_row", 90)
    ui.add_grid_column_weighted_fill("new_file.name_row", 1)
    ui.add_grid_row_fill("new_file.name_row")
    ui.create_label("new_file.name_label", "File name:", 13)
    ui.create_text_input("new_file.filename", ph_map.get(tech, "new_file"))
    ui.place_grid_child("new_file.name_row", "new_file.name_label", 0, 0)
    ui.place_grid_child("new_file.name_row", "new_file.filename", 1, 0)
    ui.place_grid_child("new_file.shell", "new_file.name_row", 1, 3)

    if tech == "E":
        ui.create_label("new_file.template_label", "Example template:", 13)
        ui.place_grid_child("new_file.shell", "new_file.template_label", 1, 5)
        ui.create_list_view("new_file.template_list")
        ui.set_property_number("new_file.template_list", "font_size", 13)
        ui.set_section_json("new_file.template_list", "items", _e_template_items())
        ui.place_grid_child("new_file.shell", "new_file.template_list", 1, 6)
        ui.create_label("new_file.template_summary", _e_template_summary(selected_template), 12)
        ui.place_grid_child("new_file.shell", "new_file.template_summary", 1, 7)

    ui.create_label("new_file.loc_label", "Save location:", 13)
    ui.place_grid_child("new_file.shell", "new_file.loc_label", 1, base_row)

    ui.create_grid("new_file.path_bar")
    ui.add_grid_column_weighted_fill("new_file.path_bar", 1)
    ui.add_grid_column_exact("new_file.path_bar", 36)
    ui.add_grid_column_exact("new_file.path_bar", 36)
    ui.add_grid_row_fill("new_file.path_bar")
    ui.create_label("new_file.path_display", initial_dir, 12)
    ui.create_button("new_file.nav.home", "⌂", "new_file.nav.home")
    ui.create_button("new_file.nav.up", "↑", "new_file.nav.up")
    ui.place_grid_child("new_file.path_bar", "new_file.path_display", 0, 0)
    ui.place_grid_child("new_file.path_bar", "new_file.nav.home", 1, 0)
    ui.place_grid_child("new_file.path_bar", "new_file.nav.up", 2, 0)
    ui.place_grid_child("new_file.shell", "new_file.path_bar", 1, base_row + 1)

    ui.create_grid("new_file.folder_row")
    ui.add_grid_column_weighted_fill("new_file.folder_row", 1)
    ui.add_grid_column_exact("new_file.folder_row", 104)
    ui.add_grid_row_fill("new_file.folder_row")
    ui.create_text_input("new_file.new_folder_name", "New folder name")
    ui.create_button("new_file.make_folder_btn", "New Folder", "new_file.make_folder")
    ui.place_grid_child("new_file.folder_row", "new_file.new_folder_name", 0, 0)
    ui.place_grid_child("new_file.folder_row", "new_file.make_folder_btn", 1, 0)
    ui.place_grid_child("new_file.shell", "new_file.folder_row", 1, base_row + 2)

    ui.create_list_view("new_file.folder_list")
    ui.set_property_number("new_file.folder_list", "font_size", 13)
    ui.set_section_json("new_file.folder_list", "items", _folder_items(initial_dir))
    ui.place_grid_child("new_file.shell", "new_file.folder_list", 1, base_row + 3)

    ui.create_label("new_file.error", "", 12)
    ui.place_grid_child("new_file.shell", "new_file.error", 1, base_row + 4)

    ui.create_grid("new_file.footer")
    ui.add_grid_column_weighted_fill("new_file.footer", 1)
    ui.add_grid_column_exact("new_file.footer", 80)
    ui.add_grid_column_exact("new_file.footer", 10)
    ui.add_grid_column_exact("new_file.footer", 100)
    ui.add_grid_row_fill("new_file.footer")
    ui.create_button("new_file.cancel_btn", "Cancel", "new_file.cancel")
    ui.create_button("new_file.create_btn", "Create File", "new_file.create")
    ui.place_grid_child("new_file.footer", "new_file.cancel_btn", 1, 0)
    ui.place_grid_child("new_file.footer", "new_file.create_btn", 3, 0)
    ui.place_grid_child("new_file.shell", "new_file.footer", 1, base_row + 5)

    return ui


def _folder_items(path: str) -> list:
    """Return {id, label} dicts for immediate subdirectories of path.
    Prepends '..' unless path is the filesystem root."""
    import os
    entries = []
    parent = str(Path(path).parent)
    if parent != path:
        entries.append({"id": parent, "label": ".."})
    try:
        for name in sorted(os.listdir(path), key=str.lower):
            if name.startswith("."):
                continue
            full = os.path.join(path, name)
            if os.path.isdir(full):
                entries.append({"id": full, "label": name})
    except OSError:
        pass
    return entries


def build_worker_fault_dialog(worker_name: str, fault_message: str):
    """Build a hard-fault popup for a faulted worker."""
    ui = UiDocumentBuilder()
    ui.create_window(f"Worker Fault — {worker_name}", 540, 200, "org.elara.ui.epa-ide.worker-fault")
    ui.set_theme_mode("dark")
    ui.create_grid("fault.shell")
    ui.add_grid_column_exact("fault.shell", 16)
    ui.add_grid_column_fill("fault.shell")
    ui.add_grid_column_exact("fault.shell", 16)
    ui.add_grid_row_exact("fault.shell", 16)
    ui.add_grid_row_fill("fault.shell")
    ui.add_grid_row_exact("fault.shell", 40)
    ui.create_rich_text_edit("fault.message", fault_message or "Hard fault (no message)")
    ui.create_grid("fault.buttons")
    ui.add_grid_column_fill("fault.buttons")
    ui.add_grid_column_exact("fault.buttons", 80)
    ui.add_grid_column_exact("fault.buttons", 8)
    ui.add_grid_row_fill("fault.buttons")
    ui.create_button("fault.close", "✕", "worker_fault.close")
    ui.place_grid_child("fault.buttons", "fault.close", 1, 0)
    ui.place_grid_child("fault.shell", "fault.message", 1, 1)
    ui.place_grid_child("fault.shell", "fault.buttons", 1, 2)
    ui.set_root_content("fault.shell")
    return ui


def build_error_dialog(title: str, message: str):
    """Build a simple error message dialog."""
    ui = UiDocumentBuilder()
    ui.create_window(title, 520, 220, "org.elara.ui.epa-ide.error-dialog")
    ui.set_theme_mode("dark")
    ui.create_grid("err.shell")
    ui.add_grid_column_exact("err.shell", 16)
    ui.add_grid_column_fill("err.shell")
    ui.add_grid_column_exact("err.shell", 16)
    ui.add_grid_row_exact("err.shell", 16)
    ui.add_grid_row_fill("err.shell")
    ui.add_grid_row_exact("err.shell", 40)
    ui.create_rich_text_edit("err.message", message)
    ui.create_grid("err.buttons")
    ui.add_grid_column_fill("err.buttons")
    ui.add_grid_column_exact("err.buttons", 80)
    ui.add_grid_column_exact("err.buttons", 8)
    ui.add_grid_row_fill("err.buttons")
    ui.create_button("err.ok", "OK", "error_dialog.close")
    ui.place_grid_child("err.buttons", "err.ok", 1, 0)
    ui.place_grid_child("err.shell", "err.message", 1, 1)
    ui.place_grid_child("err.shell", "err.buttons", 1, 2)
    ui.set_root_content("err.shell")
    return ui


def build_project_add_menu_dialog(items: list[dict]):
    ui = UiDocumentBuilder()
    height = max(120, 50 + len(items) * 38)
    ui.create_window("Project Actions", 320, height, "org.elara.ui.epa-ide.project-add-menu")
    ui.set_theme_mode("dark")
    ui.create_grid("project_add.shell")
    ui.add_grid_column_exact("project_add.shell", 12)
    ui.add_grid_column_fill("project_add.shell")
    ui.add_grid_column_exact("project_add.shell", 12)
    ui.add_grid_row_exact("project_add.shell", 12)
    ui.add_grid_row_exact("project_add.shell", 20)
    for _ in items:
        ui.add_grid_row_exact("project_add.shell", 34)
    ui.add_grid_row_fill("project_add.shell")
    ui.create_label("project_add.title", "Project Actions", 13)
    ui.place_grid_child("project_add.shell", "project_add.title", 1, 1)
    for idx, item in enumerate(items):
        button_id = f"project_add.item.{idx}"
        ui.create_button(button_id, item["label"], item["action"])
        ui.set_property_number(button_id, "font_size", 12)
        ui.place_grid_child("project_add.shell", button_id, 1, idx + 2)
    ui.set_root_content("project_add.shell")
    return ui


def build_ingress_profile_editor(type_name: str, fields: list):
    """Build the ingress profile editor window for type_name with the given field list."""
    ui = UiDocumentBuilder()
    ui.create_window(f"New {type_name} Profile", 640, 520, "org.elara.ui.epa-ide.ingress-profile-editor")
    ui.set_theme_mode("dark")

    # Root shell: name row | main area | button row
    ui.create_grid("ipe.shell")
    ui.add_grid_column_fill("ipe.shell")
    ui.add_grid_row_exact("ipe.shell", 40)    # 0  profile name
    ui.add_grid_row_fill("ipe.shell")          # 1  tree + form
    ui.add_grid_row_exact("ipe.shell", 44)    # 2  buttons

    # Name row
    ui.create_grid("ipe.name_row")
    ui.add_grid_column_exact("ipe.name_row", 110)
    ui.add_grid_column_fill("ipe.name_row")
    ui.add_grid_row_fill("ipe.name_row")
    ui.create_label("ipe.name_label", "Profile name:", 13)
    ui.create_text_input("ipe.name_input", "e.g. default", "")
    ui.place_grid_child("ipe.name_row", "ipe.name_label", 0, 0)
    ui.place_grid_child("ipe.name_row", "ipe.name_input", 1, 0)

    # Main area: left tree | right form
    ui.create_grid("ipe.main")
    ui.add_grid_column_exact("ipe.main", 200)
    ui.add_grid_column_exact("ipe.main", 4)   # divider gap
    ui.add_grid_column_fill("ipe.main")
    ui.add_grid_row_fill("ipe.main")

    # Field tree
    field_nodes = [
        {"id": f"ipe.field.{f}", "label": f}
        for f in fields
    ]
    ui.create_tree_view("ipe.tree")
    ui.set_section_json("ipe.tree", "nodes", [
        {"id": "ipe.tree.root", "label": type_name, "expanded": True, "children": field_nodes}
    ])

    # Field form (right side) — label + input stacked
    ui.create_grid("ipe.form")
    ui.add_grid_column_fill("ipe.form")
    ui.add_grid_row_exact("ipe.form", 24)    # 0  field name label
    ui.add_grid_row_exact("ipe.form", 36)    # 1  value input
    ui.add_grid_row_fill("ipe.form")          # 2  spacer

    first_field = fields[0] if fields else ""
    ui.create_label("ipe.field_label", first_field if first_field else "Select a field", 13)
    ui.create_text_input("ipe.field_input", "value", "0")

    ui.place_grid_child("ipe.form", "ipe.field_label", 0, 0)
    ui.place_grid_child("ipe.form", "ipe.field_input", 0, 1)

    ui.place_grid_child("ipe.main", "ipe.tree",  0, 0)
    ui.place_grid_child("ipe.main", "ipe.form",  2, 0)

    # Button row
    ui.create_grid("ipe.buttons")
    ui.add_grid_column_fill("ipe.buttons")
    ui.add_grid_column_exact("ipe.buttons", 100)
    ui.add_grid_column_exact("ipe.buttons", 8)
    ui.add_grid_column_exact("ipe.buttons", 100)
    ui.add_grid_row_fill("ipe.buttons")
    ui.create_button("ipe.cancel", "Cancel", "ipe.cancel")
    ui.create_button("ipe.save",   "Save",   "ipe.save")
    ui.place_grid_child("ipe.buttons", "ipe.cancel", 1, 0)
    ui.place_grid_child("ipe.buttons", "ipe.save",   3, 0)

    # Assemble root
    ui.place_grid_child("ipe.shell", "ipe.name_row", 0, 0)
    ui.place_grid_child("ipe.shell", "ipe.main",     0, 1)
    ui.place_grid_child("ipe.shell", "ipe.buttons",  0, 2)
    ui.set_root_content("ipe.shell")

    return ui


def start_background_worker():
    from elara_ui.multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime
    ensure_multi_cpu_runtime(thread_count=2)
    worker = MultiCpuWorkerTemplate(str(Path(__file__).resolve().parent / "workers" / "worker_template.py"), ["8"])
    worker.start()
    return worker

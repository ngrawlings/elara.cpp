import argparse
import json
import re
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder
from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError
from elara_ui.snapshot_dumper import UiSnapshotDumper
from elara_ui.repl_client import ElaraUiRepl


INITIAL_E_TABS = []


def _editor_ids(tab_id: str):
    return {
        "container": f"{tab_id}.container",
        "toolbar": f"{tab_id}.toolbar",
        "button_e": f"{tab_id}.view.e",
        "button_epa": f"{tab_id}.view.epa",
        "button_debug": f"{tab_id}.view.debug",
        "source": f"{tab_id}.source",
        "epa": f"{tab_id}.epa",
        "debug_panel": f"{tab_id}.debug.panel",
        "debug_form": f"{tab_id}.debug.form",
        "debug_ingress": f"{tab_id}.debug.ingress",
        "debug_worker": f"{tab_id}.debug.worker",
        "debug_start": f"{tab_id}.debug.start",
        "debug_meta": f"{tab_id}.debug.meta",
        "debug": f"{tab_id}.debug.trace",
        "debug_tabs": f"{tab_id}.debug.tabs",
        "debug_ghs": f"{tab_id}.debug.ghs",
        "debug_stack": f"{tab_id}.debug.stack",
        "debug_local": f"{tab_id}.debug.local",
    }


def _create_e_tab(ui: UiDocumentBuilder, tab_id: str, title: str, source_text: str):
    ids = _editor_ids(tab_id)
    ui.create_grid(ids["container"])
    ui.add_grid_column_fill(ids["container"])
    ui.add_grid_row_exact(ids["container"], 34)
    ui.add_grid_row_fill(ids["container"])

    ui.create_grid(ids["toolbar"])
    ui.add_grid_column_exact(ids["toolbar"], 54)
    ui.add_grid_column_exact(ids["toolbar"], 64)
    ui.add_grid_column_exact(ids["toolbar"], 78)
    ui.add_grid_column_fill(ids["toolbar"])
    ui.add_grid_row_fill(ids["toolbar"])
    ui.create_button(ids["button_e"], "E", f"{ids['button_e']}")
    ui.create_button(ids["button_epa"], "EPA", f"{ids['button_epa']}")
    ui.create_button(ids["button_debug"], "Debug", f"{ids['button_debug']}")
    ui.set_property_bool(ids["button_e"], "enabled", False)
    ui.place_grid_child(ids["toolbar"], ids["button_e"], 0, 0)
    ui.place_grid_child(ids["toolbar"], ids["button_epa"], 1, 0)
    ui.place_grid_child(ids["toolbar"], ids["button_debug"], 2, 0)
    ui.place_grid_child(ids["container"], ids["toolbar"], 0, 0)

    ui.create_code_editor(ids["source"], source_text)
    ui.create_code_editor(ids["epa"], "")
    ui.create_grid(ids["debug_panel"])
    ui.add_grid_column_weighted_fill(ids["debug_panel"], 2)
    ui.add_grid_column_exact(ids["debug_panel"], 320)
    ui.set_grid_column_border_resizable(ids["debug_panel"], 0, True)
    ui.add_grid_row_fill(ids["debug_panel"])
    ui.create_grid(ids["debug_form"])
    ui.add_grid_column_fill(ids["debug_form"])
    ui.add_grid_column_fill(ids["debug_form"])
    ui.add_grid_column_exact(ids["debug_form"], 110)
    ui.add_grid_row_exact(ids["debug_form"], 22)
    ui.add_grid_row_exact(ids["debug_form"], 28)
    ui.add_grid_row_fill(ids["debug_form"])
    ui.create_label(f"{ids['debug_ingress']}.label", "Ingress Type", 12)
    ui.create_label(f"{ids['debug_worker']}.label", "Worker", 12)
    ui.create_text_input(ids["debug_ingress"], "Packet", "")
    ui.create_text_input(ids["debug_worker"], "worker_ingest", "")
    ui.create_button(ids["debug_start"], "Start", ids["debug_start"])
    ui.create_code_editor(ids["debug_meta"], "", font_size=12)
    ui.create_tabs(ids["debug_tabs"])
    ui.create_code_editor(ids["debug"], "", font_size=13)
    ui.create_tree_view(ids["debug_ghs"])
    ui.create_tree_view(ids["debug_stack"])
    ui.create_tree_view(ids["debug_local"])
    ui.set_section_json(ids["debug_ghs"], "nodes", [{"id": f"{ids['debug_ghs']}.root", "label": "GHS Layout", "expanded": True}])
    ui.set_section_json(ids["debug_stack"], "nodes", [{"id": f"{ids['debug_stack']}.root", "label": "Stack Interpretation", "expanded": True}])
    ui.set_section_json(ids["debug_local"], "nodes", [{"id": f"{ids['debug_local']}.root", "label": "Local Arena", "expanded": True}])
    ui.add_tab(ids["debug_tabs"], "Trace", ids["debug"])
    ui.add_tab(ids["debug_tabs"], "GHS", ids["debug_ghs"])
    ui.add_tab(ids["debug_tabs"], "Stack", ids["debug_stack"])
    ui.add_tab(ids["debug_tabs"], "Local Arena", ids["debug_local"])
    ui.set_property_bool(ids["debug_meta"], "read_only", True)
    ui.set_property_bool(ids["epa"], "read_only", True)
    ui.set_property_bool(ids["debug"], "read_only", True)
    ui.set_property_bool(ids["epa"], "visible", False)
    ui.set_property_bool(ids["debug_panel"], "visible", False)
    ui.place_grid_child(ids["debug_form"], f"{ids['debug_ingress']}.label", 0, 0)
    ui.place_grid_child(ids["debug_form"], f"{ids['debug_worker']}.label", 1, 0)
    ui.place_grid_child(ids["debug_form"], ids["debug_ingress"], 0, 1)
    ui.place_grid_child(ids["debug_form"], ids["debug_worker"], 1, 1)
    ui.place_grid_child(ids["debug_form"], ids["debug_start"], 2, 1)
    ui.place_grid_child(ids["debug_form"], ids["debug_meta"], 0, 2, 3, 1)
    ui.place_grid_child(ids["debug_panel"], ids["debug_form"], 0, 0)
    ui.place_grid_child(ids["debug_panel"], ids["debug_tabs"], 1, 0)
    ui.place_grid_child(ids["container"], ids["source"], 0, 1)
    ui.place_grid_child(ids["container"], ids["epa"], 0, 1)
    ui.place_grid_child(ids["container"], ids["debug_panel"], 0, 1)
    ui.add_tab("editor.tabs", title, ids["container"],
               button_glyph="×", button_action=f"tab.close.{tab_id}")


def build_document():
    ui = UiDocumentBuilder()
    ui.create_window("EpaIde", 1080, 760, "org.elara.ui.epa-ide")
    ui.set_theme_mode("dark")
    ui.create_grid("app.shell")
    ui.add_grid_column_exact("app.shell", 56)
    ui.add_grid_column_exact("app.shell", 220)
    ui.add_grid_column_weighted_fill("app.shell", 3)
    ui.add_grid_column_exact("app.shell", 320)
    ui.set_grid_column_border_resizable("app.shell", 1, True)
    ui.set_grid_column_border_resizable("app.shell", 2, True)
    ui.add_grid_row_exact("app.shell", 32)
    ui.add_grid_row_fill("app.shell")
    ui.set_root_content("app.shell")
    ui.create_menu_bar("app.menu")
    ui.set_property_number("app.menu", "font_size", 14)
    ui.set_menu_bar_menus("app.menu", [
        {"id": "file", "label": "&File", "items": [
            {"id": "file.new_file", "label": "&New File", "shortcut": "Ctrl+N"},
            {"id": "file.new_project", "label": "New &Project...", "shortcut": "Ctrl+Shift+N"},
            {"id": "file.open_project", "label": "Open P&roject...", "shortcut": "Ctrl+Shift+O"},
            {"separator": True},
            {"id": "file.open", "label": "&Open...", "shortcut": "Ctrl+O"},
            {"id": "file.open_recent", "label": "Open &Recent", "items": [
                {"id": "file.open_recent.runtime", "label": "runtime.eproj"},
                {"id": "file.open_recent.renderer", "label": "renderer.eproj"},
                {"id": "file.open_recent.samples", "label": "samples/game.eproj"},
                {"separator": True},
                {"id": "file.open_recent.clear", "label": "&Clear Recent Projects"}
            ]},
            {"separator": True},
            {"id": "file.save", "label": "&Save", "shortcut": "Ctrl+S"},
            {"id": "file.save_as", "label": "Save &As...", "shortcut": "Ctrl+Shift+S"},
            {"id": "file.save_all", "label": "Save A&ll", "shortcut": "Ctrl+Alt+S"},
            {"separator": True},
            {"id": "file.close", "label": "&Close", "shortcut": "Ctrl+W"},
            {"id": "file.close_all", "label": "Close A&ll"},
            {"separator": True},
            {"id": "file.exit", "label": "E&xit"}
        ]},
        {"id": "edit", "label": "&Edit", "items": [
            {"id": "edit.undo", "label": "&Undo", "shortcut": "Ctrl+Z"},
            {"id": "edit.redo", "label": "&Redo", "shortcut": "Ctrl+Y"},
            {"separator": True},
            {"id": "edit.cut", "label": "Cu&t", "shortcut": "Ctrl+X"},
            {"id": "edit.copy", "label": "&Copy", "shortcut": "Ctrl+C"},
            {"id": "edit.paste", "label": "&Paste", "shortcut": "Ctrl+V"},
            {"separator": True},
            {"id": "edit.find", "label": "&Find", "shortcut": "Ctrl+F"},
            {"id": "edit.replace", "label": "&Replace", "shortcut": "Ctrl+H"},
            {"separator": True},
            {"id": "edit.preferences", "label": "Prefere&nces...", "shortcut": "Ctrl+,"}
        ]},
        {"id": "view", "label": "&View", "items": [
            {"id": "view.command_palette", "label": "Command &Palette...", "shortcut": "Ctrl+Shift+P"},
            {"separator": True},
            {"id": "view.appearance", "label": "&Appearance", "items": [
                {"id": "view.appearance.zen", "label": "Zen &Mode"},
                {"id": "view.appearance.full_screen", "label": "Full &Screen", "shortcut": "F11"},
                {"separator": True},
                {"id": "view.appearance.sidebar", "label": "Toggle &Sidebar", "shortcut": "Ctrl+B"}
            ]},
            {"id": "view.panels", "label": "&Panels", "items": [
                {"id": "view.panels.problems", "label": "&Problems"},
                {"id": "view.panels.terminal", "label": "&Terminal"},
                {"id": "view.panels.output", "label": "&Output"}
            ]}
        ]},
        {"id": "navigate", "label": "&Navigate", "items": [
            {"id": "navigate.back", "label": "&Back", "shortcut": "Alt+Left"},
            {"id": "navigate.forward", "label": "&Forward", "shortcut": "Alt+Right"},
            {"separator": True},
            {"id": "navigate.symbol", "label": "Go to &Symbol...", "shortcut": "Ctrl+Shift+O"},
            {"id": "navigate.definition", "label": "Go to &Definition", "shortcut": "F12"}
        ]},
        {"id": "code", "label": "&Code", "items": [
            {"id": "code.format", "label": "&Format Document", "shortcut": "Shift+Alt+F"},
            {"id": "code.rename", "label": "&Rename Symbol", "shortcut": "F2"},
            {"separator": True},
            {"id": "code.refactor", "label": "Re&factor", "items": [
                {"id": "code.refactor.extract_function", "label": "Extract &Function"},
                {"id": "code.refactor.extract_variable", "label": "Extract &Variable"},
                {"id": "code.refactor.inline", "label": "&Inline"}
            ]}
        ]},
        {"id": "build", "label": "&Build", "items": [
            {"id": "build.compile", "label": "&Compile Current File", "shortcut": "Ctrl+F7"},
            {"id": "build.build_project", "label": "&Build Project", "shortcut": "F7"},
            {"id": "build.rebuild_project", "label": "&Rebuild Project", "shortcut": "Ctrl+Shift+F7"},
            {"separator": True},
            {"id": "build.clean", "label": "C&lean"}
        ]},
        {"id": "run", "label": "&Run", "items": [
            {"id": "run.start", "label": "&Run", "shortcut": "F5"},
            {"id": "run.debug", "label": "&Debug", "shortcut": "Shift+F5"},
            {"id": "run.stop", "label": "S&top", "shortcut": "Ctrl+F5"},
            {"separator": True},
            {"id": "run.configurations", "label": "Run &Configurations..."}
        ]},
        {"id": "tools", "label": "&Tools", "items": [
            {"id": "tools.package_manager", "label": "&Package Manager"},
            {"id": "tools.profiler", "label": "P&rofiler"},
            {"id": "tools.memory_viewer", "label": "&Memory Viewer"}
        ]},
        {"id": "window", "label": "&Window", "items": [
            {"id": "window.next_tab", "label": "&Next Tab", "shortcut": "Ctrl+Tab"},
            {"id": "window.previous_tab", "label": "&Previous Tab", "shortcut": "Ctrl+Shift+Tab"},
            {"separator": True},
            {"id": "window.reset_layout", "label": "&Reset Layout"}
        ]},
        {"id": "help", "label": "&Help", "items": [
            {"id": "help.docs", "label": "&Documentation"},
            {"id": "help.samples", "label": "&Sample Projects"},
            {"separator": True},
            {"id": "help.about", "label": "&About EpaIde"}
        ]}
    ])
    ui.create_tree_view("nav.tree")
    ui.set_property_number("nav.tree", "font_size", 14)
    ui.set_section_json("nav.tree", "nodes", [
        {
            "id": "workspace.e",
            "label": "E",
            "expanded": True,
            "buttons": [{"glyph": "+", "action": "new_file.E"}],
            "children": [
                {
                    "id": "workspace.e.runtime",
                    "label": "runtime",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.e.runtime.main", "label": "main.e"},
                        {"id": "workspace.e.runtime.ai", "label": "assistant.e"},
                        {"id": "workspace.e.runtime.render", "label": "renderer.e"}
                    ]
                },
                {
                    "id": "workspace.e.samples",
                    "label": "samples",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.e.samples.game", "label": "game.e"},
                        {"id": "workspace.e.samples.physics", "label": "physics.e"}
                    ]
                },
                {"id": "workspace.e.project", "label": "game.eproj"}
            ]
        },
        {
            "id": "workspace.cpp",
            "label": "C++",
            "expanded": True,
            "buttons": [{"glyph": "+", "action": "new_file.Cpp"}],
            "children": [
                {
                    "id": "workspace.cpp.host",
                    "label": "host",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.cpp.host.runtime", "label": "runtime_host.cpp"},
                        {"id": "workspace.cpp.host.bridge", "label": "epa_bridge.cpp"}
                    ]
                },
                {
                    "id": "workspace.cpp.native",
                    "label": "native",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.cpp.native.worker", "label": "asset_worker.cpp"},
                        {"id": "workspace.cpp.native.simd", "label": "simd_primitives.cpp"}
                    ]
                }
            ]
        },
        {
            "id": "workspace.python",
            "label": "Python",
            "expanded": True,
            "buttons": [{"glyph": "+", "action": "new_file.Python"}],
            "children": [
                {
                    "id": "workspace.python.tools",
                    "label": "tools",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.python.tools.editor", "label": "editor_logic.py"},
                        {"id": "workspace.python.tools.agent", "label": "agent_console.py"}
                    ]
                },
                {
                    "id": "workspace.python.scripts",
                    "label": "scripts",
                    "expanded": True,
                    "children": [
                        {"id": "workspace.python.scripts.importer", "label": "import_assets.py"},
                        {"id": "workspace.python.scripts.exporter", "label": "export_project.py"}
                    ]
                }
            ]
        }
    ])

    ui.create_grid("editor.welcome")
    ui.add_grid_column_weighted_fill("editor.welcome", 1)
    ui.add_grid_row_weighted_fill("editor.welcome", 1)
    ui.add_grid_row_exact("editor.welcome", 280)
    ui.add_grid_row_weighted_fill("editor.welcome", 1)
    ui.create_rich_text_edit(
        "editor.welcome.message",
        "# The Elara Core\n\n"
        "Welcome to the Elara Parallel Assembly IDE.\n\n"
        "Open a project or create a new one to get started.\n\n"
        "---\n\n"
        "**Elara** is a parallel execution environment built around the EPA virtual machine. "
        "Workers run in isolated address spaces, communicating through typed ingress packets "
        "and signal mailboxes. The kernel coordinates scheduling and GHS memory transfer.\n\n"
        "Use the **File** menu or the buttons in the sidebar to open or create a project.",
    )
    ui.set_property_number("editor.welcome.message", "font_size", 14)
    ui.set_property_bool("editor.welcome.message", "read_only", True)
    ui.place_grid_child("editor.welcome", "editor.welcome.message", 0, 1)

    ui.create_tabs("editor.tabs")
    for tab_id, title, source_text in INITIAL_E_TABS:
        _create_e_tab(ui, tab_id, title, source_text)

    ui.create_grid("ai.panel")
    ui.add_grid_column_fill("ai.panel")
    ui.add_grid_row_weighted_fill("ai.panel", 2)
    ui.add_grid_row_weighted_fill("ai.panel", 1)
    ui.set_grid_row_border_resizable("ai.panel", 0, True)
    ui.create_rich_text_edit("ai.context", "# AI Context\n\n- Build target: EPA desktop runtime\n- Current file: main.e\n- Focus: resizable grid splitters\n\nAssistant analysis and responses appear here.\n")
    ui.set_property_number("ai.context", "font_size", 13)
    ui.create_rich_text_edit("ai.prompt", "# Prompt Draft\n\nDescribe the gameplay loop and ask the agent to scaffold the next system here.\n")
    ui.set_property_number("ai.prompt", "font_size", 13)
    ui.place_grid_child("ai.panel", "ai.context", 0, 0)
    ui.place_grid_child("ai.panel", "ai.prompt", 0, 1)

    ui.create_toolbar("app.toolbar", orientation="vertical")
    ui.set_property_number("app.toolbar", "font_size", 11)
    ui.set_property_number("app.toolbar", "item_padding_x", 6)
    ui.set_property_number("app.toolbar", "item_padding_y", 10)
    ui.set_property_number("app.toolbar", "item_spacing", 2)
    ui.add_toolbar_item("app.toolbar", "toolbar.files", "Files")
    ui.add_toolbar_item("app.toolbar", "toolbar.search", "Search")
    ui.add_toolbar_item("app.toolbar", "toolbar.repo", "Repo")
    ui.add_toolbar_separator("app.toolbar")
    ui.add_toolbar_item("app.toolbar", "toolbar.debug", "Debug")
    ui.create_grid("nav.no_project")
    ui.add_grid_column_weighted_fill("nav.no_project", 1)
    ui.add_grid_row_weighted_fill("nav.no_project", 1)
    ui.add_grid_row_exact("nav.no_project", 40)
    ui.add_grid_row_exact("nav.no_project", 12)
    ui.add_grid_row_exact("nav.no_project", 40)
    ui.add_grid_row_weighted_fill("nav.no_project", 1)
    ui.create_button("nav.no_project.new", "New Project", "no_project.new_project")
    ui.create_button("nav.no_project.open", "Open Project", "no_project.open_project")
    ui.place_grid_child("nav.no_project", "nav.no_project.new", 0, 1)
    ui.place_grid_child("nav.no_project", "nav.no_project.open", 0, 3)
    ui.place_grid_child("app.shell", "app.menu", 0, 0, 4, 1)
    ui.place_grid_child("app.shell", "app.toolbar", 0, 1)
    ui.place_grid_child("app.shell", "nav.no_project", 1, 1)
    ui.place_grid_child("app.shell", "nav.tree", 1, 1)
    ui.place_grid_child("app.shell", "editor.welcome", 2, 1)
    ui.place_grid_child("app.shell", "editor.tabs", 2, 1)
    ui.place_grid_child("app.shell", "ai.panel", 3, 1)
    return ui


def build_open_file_dialog():
    """Build a high-quality open-file dialog using the current Elara UI API.

    Current UI API limitations are represented as deliberate placeholders rather
    than fake controls:
    - no native file-system model yet, so lists are sample data
    - no table/header/list columns yet, so the file browser is a rich list label
    - no combo box yet, so file type is represented by a text input
    - no checkbox yet in this dialog flow, so option toggles are label placeholders
    - no modal default/escape button metadata yet, so actions are plain buttons
    """
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
    ui.create_text_input("dialog.location", "Location", "/home/user/Projects/E")
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
    ui.create_label("dialog.breadcrumb", "Home › Projects › E", 13)
    ui.create_button("dialog.new_folder", "New Folder", "dialog.folder.new")
    ui.create_button("dialog.refresh", "Refresh", "dialog.folder.refresh")
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.breadcrumb", 0, 0)
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.new_folder", 1, 0)
    ui.place_grid_child("dialog.breadcrumb_bar", "dialog.refresh", 2, 0)

    ui.create_list_view("dialog.files")
    ui.set_property_number("dialog.files", "font_size", 14)
    ui.set_section_json("dialog.files", "items", [
        {"id": "file.parent", "label": "..                                      Folder        —             —"},
        {"id": "file.runtime", "label": "runtime/                                Folder        Today         —"},
        {"id": "file.samples", "label": "samples/                                Folder        Yesterday      —"},
        {"id": "file.main", "label": "main.e                                  E source      Today         4 KB"},
        {"id": "file.assistant", "label": "assistant.e                             E source      Today         7 KB"},
        {"id": "file.renderer", "label": "renderer.e                              E source      Yesterday      12 KB"},
        {"id": "file.runtime_host", "label": "runtime_host.cpp                       C++ source    Monday         18 KB"},
        {"id": "file.editor_logic", "label": "editor_logic.py                         Python       Monday         9 KB"},
        {"id": "file.game_project", "label": "game.eproj                              Project       Last week      2 KB"}
    ])

    ui.create_label("dialog.file_header", "Name                                    Type          Modified       Size", 12)
    ui.create_label("dialog.file_status", "9 items     Placeholder: list_view has no native columns, sort headers, multi-select, or filesystem binding yet", 11)
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
    ui.create_window("New Project", 500, 580, "org.elara.ui.epa-ide.new-project")
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
    ui.add_grid_row_exact("wizard.shell", 20)   # 10 location label
    ui.add_grid_row_exact("wizard.shell", 34)   # 11 path bar
    ui.add_grid_row_fill("wizard.shell")        # 12 folder list
    ui.add_grid_row_exact("wizard.shell", 24)   # 13 error/validation
    ui.add_grid_row_exact("wizard.shell", 52)   # 14 footer
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

    # ── Save location ──────────────────────────────────────────────────
    ui.create_label("wizard.loc_label", "Save location:", 13)
    ui.place_grid_child("wizard.shell", "wizard.loc_label", 1, 10)

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
    ui.place_grid_child("wizard.shell", "wizard.path_bar", 1, 11)

    # Folder list — populated with subdirs of initial_path
    ui.create_list_view("wizard.folder_list")
    ui.set_property_number("wizard.folder_list", "font_size", 13)
    ui.set_section_json("wizard.folder_list", "items",
                        _folder_items(initial_path))
    ui.place_grid_child("wizard.shell", "wizard.folder_list", 1, 12)

    # ── Validation message ─────────────────────────────────────────────
    ui.create_label("wizard.error", "", 12)
    ui.place_grid_child("wizard.shell", "wizard.error", 1, 13)

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
    ui.place_grid_child("wizard.shell", "wizard.footer", 1, 14)

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
        "  local byte[64] target_kernel_id;\n"
        "  int count = clamp_count(payload.counter);\n"
        "  loop_count = count;\n"
        "  while (loop_count) {\n"
        "    loop_count = loop_count - 1;\n"
        "  }\n"
        "  // TODO: fill target_kernel_id with a kernel id string.\n"
        "  // TODO: populate outbound from worker-local state before far_signal().\n"
        "  far_signal(target_kernel_id, outbound);\n"
        "  host_signal();\n"
        "  kernel_signal();\n"
        "}\n"
    )


def _e_child_kernel_router_template(file_name: str) -> str:
    stem = Path(file_name).stem
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
        "  local byte[96] target_kernel_id;\n"
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
        "  // TODO: stage a target kernel id into target_kernel_id.\n"
        "  // TODO: fill outbound as a staged local-area message.\n"
        "  far_signal(target_kernel_id, outbound);\n"
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
    return ""


def build_new_file_dialog(tech: str, initial_dir: str, selected_template: str | None = None):
    ph_map    = {"E": "my_module.e", "Cpp": "my_module.cpp", "Python": "my_module.py"}
    label_map = {"E": "E", "Cpp": "C++", "Python": "Python"}
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


def start_background_worker():
    from elara_ui.multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime
    ensure_multi_cpu_runtime(thread_count=2)
    worker = MultiCpuWorkerTemplate(str(Path(__file__).resolve().parent / "workers" / "worker_template.py"), ["8"])
    worker.start()
    return worker


def main():
    parser = argparse.ArgumentParser(description="Load the generated Elara UI document into a running RPC head")
    parser.add_argument("--host", default="127.0.0.1", help="RPC server host")
    parser.add_argument("--port", default=18777, type=int, help="RPC server port")
    parser.add_argument("--snapshot", action="store_true", help="Fetch a root snapshot after loading")
    parser.add_argument("--dump-snapshot", action="store_true", help="Dump the full runtime widget snapshot to JSON after loading")
    parser.add_argument("--snapshot-out", default="elara-ui-snapshot.json", help="Path used by --dump-snapshot and the integrated REPL snapshot command")
    parser.add_argument("--repl", action="store_true", help="Start the integrated Elara UI REPL after loading the document")
    parser.add_argument("--output", help="Write the generated document JSON to this path")
    parser.add_argument("--once", action="store_true", help="Load once and exit immediately")
    parser.add_argument("--no-events", action="store_true", help="Do not subscribe to default UI events")
    parser.add_argument("--no-worker", action="store_true", help="Do not start the optional multi-core worker template")
    parser.add_argument("--event-log", default=None, help="Write all received UI events to this JSONL file")
    args = parser.parse_args()

    client_ref = {}
    wizard_state = {}            # live checkbox state for the new-project wizard
    nav_state = {}               # current browse path in the wizard file picker
    open_project_nav_state = {}  # current browse path in the open-project dialog
    app_state = {}               # persistent project state set after universal creation
    new_file_state = {}          # live state for the new-file dialog
    new_file_nav_state = {}      # current browse path in the new-file dialog
    editor_state = {}
    app_state["active_editor_tab"] = INITIAL_E_TABS[0][0] if INITIAL_E_TABS else ""
    tab_list = []                # [{"tab_id", "path", "index", "preview"}]
    tab_click_state = {}         # double-click detection: {"path", "time"}

    def _compiler_root():
        return Path(__file__).resolve().parent.parent / "libElaraParallelAssembly" / "e"

    def _compiler_binary():
        return _compiler_root() / ".." / "build" / "e" / "e2epa"

    def _semantic_binary():
        return _compiler_root() / ".." / "build" / "e" / "ec"

    def _ensure_e2epa():
        compiler = _compiler_binary()
        if compiler.is_file():
            return compiler

        subprocess.run(
            ["make", "-C", str(_compiler_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return compiler

    def _ensure_ec():
        semantic = _semantic_binary()
        if semantic.is_file():
            return semantic
        _ensure_e2epa()
        return semantic

    def _extract_section_block(text: str, heading: str):
        lines = text.splitlines()
        start = None
        for idx, line in enumerate(lines):
            if line.strip() == heading:
                start = idx
                break
        if start is None:
            return ""
        end = len(lines)
        for idx in range(start + 1, len(lines)):
            line = lines[idx]
            if line and not line.startswith(" "):
                end = idx
                break
        return "\n".join(lines[start:end]).strip()

    def _replace_tree_nodes(client, target: str, nodes):
        document = json.dumps({"nodes": nodes}, separators=(",", ":"))
        client.call("ui.replaceChildren", {"target": target, "document": document})

    def _parse_tree_lines(block: str, root_label: str, root_id: str):
        root = {"id": root_id, "label": root_label, "expanded": True, "children": []}
        if not block.strip():
            root["children"].append({"id": f"{root_id}.empty", "label": "Unavailable"})
            return [root]
        stack = [(-1, root)]
        for index, raw_line in enumerate(block.splitlines()):
            if not raw_line.strip():
                continue
            indent = len(raw_line) - len(raw_line.lstrip(" "))
            node = {
                "id": f"{root_id}.{index}",
                "label": raw_line.strip(),
            }
            while len(stack) > 1 and indent <= stack[-1][0]:
                stack.pop()
            parent = stack[-1][1]
            parent.setdefault("children", []).append(node)
            stack.append((indent, node))
        return [root]

    def _extract_debug_candidates(source_text: str):
        type_names = re.findall(r"^\s*type\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", source_text, flags=re.MULTILINE)
        worker_names = re.findall(r"^\s*worker\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", source_text, flags=re.MULTILINE)
        seen = set()
        ordered_types = []
        for name in type_names:
            if name not in seen:
                seen.add(name)
                ordered_types.append(name)
        seen.clear()
        ordered_workers = []
        for name in worker_names:
            if name not in seen:
                seen.add(name)
                ordered_workers.append(name)
        return ordered_types, ordered_workers

    def _first_marker_line(epa_text: str):
        lines = epa_text.splitlines()
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("WAIT_FOR_DATA"):
                return idx
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("ENTRY_START") or stripped.startswith("FUNC_START"):
                return idx
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped and not stripped.startswith(";"):
                return idx
        return 0

    def _debug_preview_text(epa_text: str, marker_line: int | None = None, radius: int = 5):
        if not epa_text.strip():
            return "# Debug Trace\n\nNo EPA output available.\n"
        lines = epa_text.splitlines()
        marker = _first_marker_line(epa_text) if marker_line is None else max(0, min(marker_line, len(lines) - 1))
        start = max(0, marker - radius)
        end = min(len(lines), marker + radius + 1)
        out = [f"# Debug Trace", "", f"marker_line={marker + 1}", ""]
        width = len(str(end))
        for idx in range(start, end):
            prefix = ">>" if idx == marker else "  "
            out.append(f"{prefix} {idx + 1:>{width}} | {lines[idx]}")
        return "\n".join(out) + "\n"

    def _analyze_e_source(source_text: str, ids: dict):
        semantic = _ensure_ec()
        with tempfile.TemporaryDirectory(prefix="epa-ide-ec-") as tmp:
            tmp_path = Path(tmp)
            source_path = tmp_path / "buffer.e"
            source_path.write_text(source_text, encoding="utf-8")
            proc = subprocess.run(
                [str(semantic), str(source_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if proc.returncode != 0:
                message = (proc.stderr or proc.stdout or "semantic analysis failed").strip()
                return {
                    "ok": False,
                    "message": message,
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                }

            stdout = proc.stdout or ""
            ghs_block = _extract_section_block(stdout, "type-ghs-layouts")
            frame_block = _extract_section_block(stdout, "function-ghs-frames")
            local_lines = []
            stack_lines = []
            for line in frame_block.splitlines():
                stripped = line.strip()
                if "storage=local-scope-arena" in stripped:
                    local_lines.append(stripped)
                elif stripped.startswith("local ") or stripped.startswith("func "):
                    stack_lines.append(stripped)
            return {
                "ok": True,
                "message": "",
                "ghs_nodes": _parse_tree_lines(ghs_block or "No declared GHS layouts.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("\n".join(stack_lines).strip() or "No stack frame data.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("\n".join(local_lines).strip() or "No local arena allocations.", "Local Arena", f"{ids['debug_local']}.root"),
            }

    def _diagnostic_from_error(message: str):
        match = re.search(r"\bat (\d+):(\d+)\b", message)
        if not match:
            match = re.search(r"\b(\d+):(\d+)\b", message)
        if not match:
            return []

        line = max(0, int(match.group(1)) - 1)
        column = max(0, int(match.group(2)) - 1)
        token_match = re.search(r"near '([^']*)'", message)
        length = 1
        if token_match:
            token = token_match.group(1)
            if token:
                length = len(token)
        return [{
            "line": line,
            "column": column,
            "length": max(1, length),
            "message": message.strip(),
        }]

    def _compile_e_source(source_text: str):
        compiler = _ensure_e2epa()
        with tempfile.TemporaryDirectory(prefix="epa-ide-e2epa-") as tmp:
            tmp_path = Path(tmp)
            source_path = tmp_path / "buffer.e"
            output_path = tmp_path / "buffer.epaasm"
            source_path.write_text(source_text, encoding="utf-8")
            proc = subprocess.run(
                [str(compiler), str(source_path), str(output_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if proc.returncode == 0:
                epa_text = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
                return {"ok": True, "epa_text": epa_text, "diagnostics": [], "message": ""}
            message = (proc.stderr or proc.stdout or "compile failed").strip()
            return {
                "ok": False,
                "epa_text": "",
                "diagnostics": _diagnostic_from_error(message),
                "message": message,
            }

    def _apply_editor_view(client, tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        view = state.get("view", "e")
        is_epa = view == "epa"
        is_debug = view == "debug"
        client.set_visible(ids["source"], not is_epa and not is_debug)
        client.set_visible(ids["epa"], is_epa)
        client.set_visible(ids["debug_panel"], is_debug)
        client.set_enabled(ids["button_e"], view != "e")
        client.set_enabled(ids["button_epa"], view != "epa")
        client.set_enabled(ids["button_debug"], view != "debug")
        client.set_read_only(ids["epa"], True)
        client.set_read_only(ids["debug"], True)
        client.set_read_only(ids["debug_meta"], True)

    def _refresh_debug_controls(client, tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        meta_lines = [
            "# Debug Setup",
            "",
            "Select the ingress type and the worker to inject after kernel startup.",
            "",
            "Available ingress types:",
        ]
        if state.get("available_types"):
            meta_lines.extend(f"- {name}" for name in state["available_types"])
        else:
            meta_lines.append("- none discovered")
        meta_lines.append("")
        meta_lines.append("Available workers:")
        if state.get("available_workers"):
            meta_lines.extend(f"- {name}" for name in state["available_workers"])
        else:
            meta_lines.append("- none discovered")
        if state.get("debug_started"):
            meta_lines.extend([
                "",
                f"Prepared ingress type: {state.get('debug_ingress_type', '') or '(unset)'}",
                f"Prepared worker: {state.get('debug_worker_name', '') or '(unset)'}",
            ])
        client.set_text(ids["debug_ingress"], state.get("debug_ingress_type", ""))
        client.set_text(ids["debug_worker"], state.get("debug_worker_name", ""))
        client.set_text(ids["debug_meta"], "\n".join(meta_lines) + "\n")

    def _refresh_debug_sidebars(client, tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        _replace_tree_nodes(client, ids["debug_ghs"], state.get("ghs_nodes", _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root")))
        _replace_tree_nodes(client, ids["debug_stack"], state.get("stack_nodes", _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root")))
        _replace_tree_nodes(client, ids["debug_local"], state.get("local_nodes", _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root")))

    def _refresh_e_tab(client, tab_id: str, expected_seq: int | None = None):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        source_text = state.get("source_text", "")
        try:
            result = _compile_e_source(source_text)
        except Exception as exc:
            result = {
                "ok": False,
                "epa_text": "",
                "diagnostics": [],
                "message": str(exc),
            }
        if expected_seq is not None:
            current = editor_state.get(tab_id)
            if not current or current.get("compile_seq") != expected_seq:
                return
        state["epa_text"] = result["epa_text"]
        state["compile_error"] = result["message"]
        state["available_types"], state["available_workers"] = _extract_debug_candidates(source_text)
        if not state.get("debug_ingress_type") and state["available_types"]:
            state["debug_ingress_type"] = state["available_types"][0]
        if not state.get("debug_worker_name") and state["available_workers"]:
            state["debug_worker_name"] = state["available_workers"][0]
        state["debug_marker_line"] = _first_marker_line(result["epa_text"]) if result["ok"] else 0
        state["debug_text"] = (
            _debug_preview_text(result["epa_text"], state.get("debug_marker_line"))
            if result["ok"] else
            "# Debug Trace\n\nEPA view is blank because compilation failed.\n"
        )
        if result["ok"]:
            try:
                semantic = _analyze_e_source(source_text, ids)
            except Exception as exc:
                semantic = {
                    "ok": False,
                    "message": str(exc),
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                }
        else:
            semantic = {
                "ok": False,
                "message": result["message"],
                "ghs_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Local Arena", f"{ids['debug_local']}.root"),
            }
        state["ghs_nodes"] = semantic["ghs_nodes"]
        state["stack_nodes"] = semantic["stack_nodes"]
        state["local_nodes"] = semantic["local_nodes"]
        client.set_text(ids["epa"], result["epa_text"] if result["ok"] else "")
        client.set_text(ids["debug"], state["debug_text"])
        client.set_code_editor_diagnostics(ids["source"], result["diagnostics"])
        _apply_editor_view(client, tab_id)
        _refresh_debug_controls(client, tab_id)
        if app_state.get("active_editor_tab") == tab_id:
            _refresh_debug_sidebars(client, tab_id)

    def _init_editor_state():
        editor_state.clear()
        for tab_id, title, source_text in INITIAL_E_TABS:
            ids = _editor_ids(tab_id)
            editor_state[tab_id] = {
                "title": title,
                "source_text": source_text,
                "epa_text": "",
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "debug_marker_line": 0,
                "debug_text": "# Debug Trace\n\nNo EPA output available.\n",
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                "available_types": [],
                "available_workers": [],
                "debug_ingress_type": "",
                "debug_worker_name": "",
                "debug_started": False,
            }

    def _wizard_navigate(client, path: str):
        """Navigate the wizard folder browser to path and refresh the list."""
        import os
        nav_state["path"] = path
        items = _folder_items(path)
        try:
            client.replace_list_items("wizard.folder_list", items)
            client.set_text("wizard.path_display", path)
            client.set_text("wizard.error", "")
        except Exception:
            pass

    def _new_file_navigate(client, path: str):
        new_file_nav_state["path"] = path
        items = _folder_items(path)
        print(f"[navigate] replacing list with {len(items)} items for {path!r}", flush=True)
        try:
            client.replace_list_items("new_file.folder_list", items)
            client.set_text("new_file.path_display", path)
            client.set_text("new_file.error", "")
            print(f"[navigate] done", flush=True)
        except Exception as e:
            print(f"[navigate error] {e}", flush=True)
            try:
                snap = client.snapshot()
                windows = (snap or {}).get("windows", [])
                for w in windows:
                    wid = w.get("window_id")
                    def _collect_ids(node, out):
                        if isinstance(node, dict):
                            nid = node.get("id")
                            if nid:
                                out.append(nid)
                            for v in node.values():
                                _collect_ids(v, out)
                        elif isinstance(node, list):
                            for v in node:
                                _collect_ids(v, out)
                    ids = []
                    _collect_ids(w.get("snapshot", {}), ids)
                    print(f"[navigate error] window={wid!r} registered_ids={ids}", flush=True)
            except Exception as snap_e:
                print(f"[navigate error] snapshot failed: {snap_e}", flush=True)

    def _open_project_navigate(client, path: str):
        """Navigate the open-project dialog to path and refresh the list."""
        open_project_nav_state["path"] = path
        items = _folder_items(path)
        try:
            client.replace_list_items("open_project.folder_list", items)
            client.set_text("open_project.path_display", path)
            client.set_text("open_project.hint", "Double-click a folder to navigate or open as project")
        except Exception:
            pass

    def _close_open_project_window(client):
        last_error = None
        for window_id in ("open-project", "open_project"):
            try:
                client.close_window(window_id)
                print(json.dumps({"open_project_window_closed": window_id}), flush=True)
                return True
            except Exception as exc:
                last_error = exc

        if last_error is not None:
            print(json.dumps({"open_project_window_close_failed": str(last_error)}), flush=True)
        return False

    def _open_project(client, project_path):
        """Populate nav.tree with the structure of an open project."""
        project_path = Path(project_path)
        meta_path = project_path / ".elaraproject" / "project.json"
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            meta = {}
        project_name = meta.get("name", project_path.name)
        technologies = meta.get("technologies", [])

        tech_action_map = {"epa": "new_file.E", "cpp": "new_file.Cpp", "python": "new_file.Python"}

        def _dir_node(path: Path) -> dict:
            node = {"id": str(path), "label": path.name, "expanded": True, "children": []}
            try:
                entries = sorted(path.iterdir(), key=lambda p: (p.is_file(), p.name.lower()))
                for entry in entries:
                    if entry.name.startswith("."):
                        continue
                    if entry.is_dir():
                        node["children"].append(_dir_node(entry))
                    else:
                        node["children"].append({"id": str(entry), "label": entry.name})
            except PermissionError:
                pass
            return node

        children = []
        for tech in technologies:
            tech_dir = project_path / tech
            if tech_dir.is_dir():
                btn_action = tech_action_map.get(tech, f"new_file.{tech}")
                node = _dir_node(tech_dir)
                node["buttons"] = [{"glyph": "+", "action": btn_action}]
                children.append(node)

        build_dir = project_path / "build"
        if build_dir.is_dir():
            children.append(_dir_node(build_dir))

        nodes = [{
            "id": str(project_path),
            "label": project_name,
            "expanded": True,
            "children": children,
        }]
        document = json.dumps({"nodes": nodes}, separators=(",", ":"))
        try:
            client.call("ui.replaceChildren", {"target": "nav.tree", "document": document})
        except Exception:
            pass
        try:
            client.set_window_title(f"EPA-IDE : {project_name}")
        except Exception:
            pass

        app_state.update({
            "project_root": str(project_path),
            "project_name": project_name,
        })
        try:
            client.call("ui.setVisible", {"target": "nav.no_project", "visible": False})
            client.call("ui.setVisible", {"target": "nav.tree", "visible": True})
            client.call("ui.setVisible", {"target": "app.toolbar", "visible": True})
        except Exception:
            pass
        _close_open_project_window(client)

    def _tab_id_for_path(path: str) -> str:
        import hashlib
        return "tab." + hashlib.md5(path.encode()).hexdigest()[:8]

    def _ext_for_path(path: str) -> str:
        return Path(path).suffix.lower()

    def _open_file_tab(client, file_path: str, make_permanent: bool = False):
        tab_id = _tab_id_for_path(file_path)
        ext = _ext_for_path(file_path)
        title = Path(file_path).name

        existing = next((t for t in tab_list if t["path"] == file_path), None)
        if existing:
            if make_permanent and existing["preview"]:
                existing["preview"] = False
            try:
                client.call("ui.setActiveTab", {"target": "editor.tabs", "index": existing["index"]})
                client.call("ui.setVisible", {"target": "editor.welcome", "visible": False})
                client.call("ui.setVisible", {"target": "editor.tabs", "visible": True})
            except Exception:
                pass
            return

        preview_entry = next((t for t in tab_list if t["preview"]), None)
        if preview_entry and not make_permanent:
            insert_index = preview_entry["index"]
            try:
                client.call("ui.removeTab", {"target": "editor.tabs", "index": insert_index})
            except Exception:
                pass
            tab_list.remove(preview_entry)
            if tab_id in editor_state:
                del editor_state[tab_id]
            for t in tab_list:
                if t["index"] >= insert_index:
                    t["index"] -= 1
        else:
            insert_index = len(tab_list)

        try:
            source_text = Path(file_path).read_text(encoding="utf-8", errors="replace")
        except Exception:
            source_text = ""

        is_preview = not make_permanent
        close_action = f"tab.close.{tab_id}"

        if ext == ".e":
            tab_ui = UiDocumentBuilder()
            tab_ui.create_tabs("editor.tabs")
            _create_e_tab(tab_ui, tab_id, title, source_text)
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)
            editor_state[tab_id] = {
                "title": title,
                "source_text": source_text,
                "epa_text": "",
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "debug_marker_line": 0,
                "debug_text": "# Debug Trace\n\nNo EPA output available.\n",
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{tab_id}.debug_ghs.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{tab_id}.debug_stack.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{tab_id}.debug_local.root"),
                "available_types": [],
                "available_workers": [],
                "debug_ingress_type": "",
                "debug_worker_name": "",
                "debug_started": False,
            }
        else:
            tab_ui = UiDocumentBuilder()
            tab_ui.create_rich_text_edit(tab_id + ".container", source_text)
            tab_ui.set_property_number(tab_id + ".container", "font_size", 13)
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)

        try:
            client.call("ui.addTab", {
                "target": "editor.tabs",
                "title": title,
                "button_glyph": "x",
                "button_action": close_action,
                "child": child_json,
            })
            tab_list.insert(insert_index, {
                "tab_id": tab_id,
                "path": file_path,
                "index": insert_index,
                "preview": is_preview,
            })
            for t in tab_list:
                if t is not tab_list[insert_index] and t["index"] >= insert_index:
                    t["index"] += 1
            client.call("ui.setActiveTab", {"target": "editor.tabs", "index": insert_index})
            client.call("ui.setVisible", {"target": "editor.welcome", "visible": False})
            client.call("ui.setVisible", {"target": "editor.tabs", "visible": True})
            if ext == ".e":
                _refresh_e_tab(client, tab_id)
        except Exception:
            pass

    def _deferred(fn):
        """Run fn on a thread so on_ui_event returns before any RPC calls are made."""
        threading.Thread(target=fn, daemon=True).start()

    def on_ui_event(params):
        client = client_ref.get("client")
        action = params.get("action")
        payload = params.get("payload") or {}
        target = params.get("target")

        for tab_id, state in editor_state.items():
            ids = _editor_ids(tab_id)
            if action == "textChanged" and target == ids["source"]:
                app_state["active_editor_tab"] = tab_id
                state["source_text"] = payload.get("text", "")
                state["compile_seq"] = int(state.get("compile_seq", 0)) + 1
                if client is not None:
                    c = client
                    current_tab = tab_id
                    seq = state["compile_seq"]
                    _deferred(lambda: _refresh_e_tab(c, current_tab, seq))
                return {"received": True}
            if action == "keysTyped" and target == ids["debug_ingress"]:
                app_state["active_editor_tab"] = tab_id
                state["debug_ingress_type"] = state.get("debug_ingress_type", "") + payload.get("text", "")
                return {"received": True}
            if action == "keysTyped" and target == ids["debug_worker"]:
                app_state["active_editor_tab"] = tab_id
                state["debug_worker_name"] = state.get("debug_worker_name", "") + payload.get("text", "")
                return {"received": True}
            if action == "keyDown" and target == ids["debug_ingress"] and payload.get("keyval", 0) == 0xff08:
                app_state["active_editor_tab"] = tab_id
                value = state.get("debug_ingress_type", "")
                if value:
                    state["debug_ingress_type"] = value[:-1]
                return {"received": True}
            if action == "keyDown" and target == ids["debug_worker"] and payload.get("keyval", 0) == 0xff08:
                app_state["active_editor_tab"] = tab_id
                value = state.get("debug_worker_name", "")
                if value:
                    state["debug_worker_name"] = value[:-1]
                return {"received": True}

        # Track technology checkbox toggles from the wizard.
        if action == "valueChanged" and target in (
            "wizard.tech.epa", "wizard.tech.cpp", "wizard.tech.python"
        ):
            key = "tech_" + target.rsplit(".", 1)[-1]
            wizard_state[key] = payload.get("value", 0) > 0.5

        # Track project name typed into the wizard text input.
        if action == "keysTyped" and target == "wizard.project_name":
            wizard_state["project_name"] = wizard_state.get("project_name", "") + payload.get("text", "")

        if action == "keyDown" and target == "wizard.project_name":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = wizard_state.get("project_name", "")
                if name:
                    wizard_state["project_name"] = name[:-1]

        # Track filename typed into the new-file dialog.
        if action == "keysTyped" and target == "new_file.filename":
            new_file_state["filename"] = new_file_state.get("filename", "") + payload.get("text", "")

        if action == "keyDown" and target == "new_file.filename":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = new_file_state.get("filename", "")
                if name:
                    new_file_state["filename"] = name[:-1]

        if action == "keysTyped" and target == "new_file.new_folder_name":
            new_file_state["new_folder_name"] = new_file_state.get("new_folder_name", "") + payload.get("text", "")

        if action == "keyDown" and target == "new_file.new_folder_name":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = new_file_state.get("new_folder_name", "")
                if name:
                    new_file_state["new_folder_name"] = name[:-1]

        # New-file dialog: double-click a folder to navigate into it.
        if target == "new_file.folder_list" and action in ("clicked", "action"):
            row_height = 23
            if action == "clicked":
                y = payload.get("y", -1.0)
                row_index = int(y / row_height) if y >= 0 else -1
                items = _folder_items(new_file_nav_state.get("path", str(Path.home())))
                row_value = items[row_index]["id"] if 0 <= row_index < len(items) else ""
                print(f"[folder_list] click row={row_index} value={row_value!r}", flush=True)
            elif action == "action":
                folder_path = payload.get("action", "")
                is_dir = Path(folder_path).is_dir() if folder_path else False
                print(f"[folder_list] double-click value={folder_path!r} is_dir={is_dir}", flush=True)
                if folder_path and is_dir and client is not None:
                    c = client
                    p = folder_path
                    print(f"[folder_list] navigating to {p!r}", flush=True)
                    _deferred(lambda: _new_file_navigate(c, p))
            return {"received": True}

        if target == "new_file.template_list" and action in ("clicked", "action"):
            template_items = _e_template_items()
            template_id = ""
            if action == "clicked":
                row_height = 23
                y = payload.get("y", -1.0)
                row_index = int(y / row_height) if y >= 0 else -1
                if 0 <= row_index < len(template_items):
                    template_id = template_items[row_index]["id"]
            else:
                template_id = payload.get("action", "")
            if template_id in E_FILE_TEMPLATES:
                new_file_state["template"] = template_id
                if client is not None:
                    c = client
                    summary = _e_template_summary(template_id)
                    _deferred(lambda: c.set_text("new_file.template_summary", summary))
            return {"received": True}

        # Tree view file click — single click = preview tab, double click = permanent tab.
        if action == "action" and target == "nav.tree" and client is not None:
            node_path = payload.get("action", "")
            if node_path and Path(node_path).is_file():
                now = time.monotonic()
                last = tab_click_state.get("path")
                last_t = tab_click_state.get("time", 0.0)
                is_double = (node_path == last and now - last_t < 0.5)
                tab_click_state["path"] = node_path
                tab_click_state["time"] = now
                c = client
                np = node_path
                perm = is_double
                _deferred(lambda: _open_file_tab(c, np, perm))
            return {"received": True}

        # Double-click on a folder item navigates into it.
        if action == "action" and target == "wizard.folder_list" and client is not None:
            folder_path = payload.get("action", "")
            if folder_path:
                c = client
                p = folder_path
                _deferred(lambda: _wizard_navigate(c, p))
            return {"received": True}

        # Open-project dialog: double-click to navigate or open as project.
        if action == "action" and target == "open_project.folder_list" and client is not None:
            folder_path = payload.get("action", "")
            if folder_path and Path(folder_path).is_dir():
                c = client
                fp = folder_path
                def _handle_folder_dclick():
                    if (Path(fp) / ".elaraproject").is_dir():
                        _open_project(c, fp)
                    else:
                        _open_project_navigate(c, fp)
                _deferred(_handle_folder_dclick)
            return {"received": True}

        if action == "action" and client is not None:
            item_action = payload.get("action")
            c = client

            for tab_id, state in editor_state.items():
                ids = _editor_ids(tab_id)
                if item_action == ids["button_e"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "e"
                    current_tab = tab_id
                    _deferred(lambda: (_apply_editor_view(c, current_tab), _refresh_debug_sidebars(c, current_tab)))
                    return {"received": True}
                if item_action == ids["button_epa"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "epa"
                    current_tab = tab_id
                    _deferred(lambda: (_apply_editor_view(c, current_tab), _refresh_debug_sidebars(c, current_tab)))
                    return {"received": True}
                if item_action == ids["button_debug"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "debug"
                    current_tab = tab_id
                    _deferred(lambda: (_apply_editor_view(c, current_tab), _refresh_debug_controls(c, current_tab), _refresh_debug_sidebars(c, current_tab)))
                    return {"received": True}
                if item_action == ids["debug_start"]:
                    app_state["active_editor_tab"] = tab_id
                    state["debug_started"] = True
                    state["debug_text"] = (
                        "# Debug Trace\n\n"
                        "Debug startup prepared.\n\n"
                        f"Ingress type: {state.get('debug_ingress_type', '') or '(unset)'}\n"
                        f"Worker: {state.get('debug_worker_name', '') or '(unset)'}\n\n"
                        "Kernel should run first, then the selected ingress payload can be injected into the selected worker.\n\n"
                        "Runtime stepping/injection is not wired yet in this first pass."
                    )
                    current_tab = tab_id
                    _deferred(lambda: (
                        c.set_text(_editor_ids(current_tab)["debug"], editor_state[current_tab]["debug_text"]),
                        _refresh_debug_controls(c, current_tab),
                        _refresh_debug_sidebars(c, current_tab),
                        _apply_editor_view(c, current_tab),
                    ))
                    return {"received": True}

            if item_action and item_action.startswith("tab.close."):
                close_tab_id = item_action[len("tab.close."):]
                entry = next((t for t in tab_list if t["tab_id"] == close_tab_id), None)
                if entry:
                    close_index = entry["index"]
                    tab_list.remove(entry)
                    if close_tab_id in editor_state:
                        del editor_state[close_tab_id]
                    for t in tab_list:
                        if t["index"] > close_index:
                            t["index"] -= 1
                    ci = close_index
                    def _do_close_tab():
                        try:
                            c.call("ui.removeTab", {"target": "editor.tabs", "index": ci})
                            if not tab_list:
                                c.call("ui.setVisible", {"target": "editor.tabs", "visible": False})
                                c.call("ui.setVisible", {"target": "editor.welcome", "visible": True})
                        except Exception:
                            pass
                    _deferred(_do_close_tab)
                return {"received": True}

            if target == "app.menu" and item_action in (
                "edit.cut", "edit.copy", "edit.paste", "edit.select_all"
            ):
                _deferred(lambda: c.perform_focused_action(item_action))

            if target == "app.menu" and item_action == "file.open":
                _deferred(lambda: c.open_window("open-file", "Open File", 920, 640, build_open_file_dialog()))

            elif item_action in ("file.new_project", "no_project.new_project"):
                initial = str(Path.home())
                wizard_state.clear()
                wizard_state.update({
                    "tech_epa": True, "tech_cpp": True, "tech_python": True,
                    "project_name": "",
                })
                nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "new-project", "New Project", 500, 580,
                    build_new_project_wizard(initial)
                ))

            elif item_action in ("file.open_project", "no_project.open_project"):
                initial = str(Path.home())
                open_project_nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "open-project", "Open Project", 500, 500,
                    build_open_project_dialog(initial)
                ))

            elif item_action == "open_project.cancel":
                _deferred(lambda: _close_open_project_window(c))

            elif item_action == "open_project.nav.up":
                current = open_project_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _open_project_navigate(c, p))

            elif item_action == "open_project.nav.home":
                _deferred(lambda: _open_project_navigate(c, str(Path.home())))

            elif item_action == "open_project.open":
                current = open_project_nav_state.get("path", str(Path.home()))
                cur = current
                def _do_open():
                    if (Path(cur) / ".elaraproject").is_dir():
                        _open_project(c, cur)
                    else:
                        try:
                            c.set_text("open_project.hint", "No .elaraproject found — navigate into a project folder")
                        except Exception:
                            pass
                _deferred(_do_open)

            elif item_action and item_action.startswith("new_file.") and item_action not in (
                "new_file.cancel", "new_file.create",
                "new_file.nav.up", "new_file.nav.home",
                "new_file.make_folder",
            ):
                tech = item_action[len("new_file."):]
                project_root = app_state.get("project_root", "")
                tech_dir_map = {"E": "epa", "Cpp": "cpp", "Python": "python"}
                tech_sub = tech_dir_map.get(tech, "")
                if project_root and tech_sub:
                    initial = str(Path(project_root) / tech_sub)
                    if not Path(initial).is_dir():
                        initial = project_root
                else:
                    initial = project_root or str(Path.home())
                new_file_state.clear()
                new_file_state.update({
                    "filename": "",
                    "new_folder_name": "",
                    "tech": tech,
                    "dir": initial,
                    "template": "root_node" if tech == "E" else "",
                })
                new_file_nav_state["path"] = initial
                dialog_height = 650 if tech == "E" else 520
                _deferred(lambda: c.open_window(
                    "new-file", "New File", 460, dialog_height,
                    build_new_file_dialog(tech, initial, new_file_state.get("template"))
                ))

            elif item_action == "new_file.cancel":
                _deferred(lambda: c.close_window("new-file"))

            elif item_action == "new_file.nav.up":
                current = new_file_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _new_file_navigate(c, p))

            elif item_action == "new_file.nav.home":
                _deferred(lambda: _new_file_navigate(c, str(Path.home())))

            elif item_action == "new_file.make_folder":
                folder_name = new_file_state.get("new_folder_name", "").strip()
                cur_dir     = new_file_nav_state.get("path", str(Path.home()))

                def _do_make_folder():
                    if not folder_name:
                        try:
                            c.set_text("new_file.error", "Folder name cannot be empty.")
                        except Exception:
                            pass
                        return
                    new_dir = Path(cur_dir) / folder_name
                    try:
                        new_dir.mkdir(parents=True, exist_ok=True)
                    except OSError as exc:
                        try:
                            c.set_text("new_file.error", f"Could not create folder: {exc}")
                        except Exception:
                            pass
                        return
                    new_file_state["new_folder_name"] = ""
                    try:
                        c.set_text("new_file.new_folder_name", "")
                    except Exception:
                        pass
                    _new_file_navigate(c, cur_dir)

                _deferred(_do_make_folder)

            elif item_action == "new_file.create":
                filename    = new_file_state.get("filename", "").strip()
                save_dir    = new_file_nav_state.get("path", new_file_state.get("dir", str(Path.home())))
                tech        = new_file_state.get("tech", "")
                template_id = new_file_state.get("template", "root_node")
                ext_map     = {"E": ".e", "Cpp": ".cpp", "Python": ".py"}
                default_ext = ext_map.get(tech, "")

                def _do_create_file():
                    if not filename:
                        try:
                            c.set_text("new_file.error", "File name cannot be empty.")
                        except Exception:
                            pass
                        return
                    name = filename if "." in filename else filename + default_ext
                    dest = Path(save_dir) / name
                    try:
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        if tech == "Cpp":
                            if dest.suffix.lower() != ".cpp":
                                dest = dest.with_suffix(".cpp")
                            header = dest.with_suffix(".h")
                            dest.write_text(_cpp_source_content(dest.name), encoding="utf-8")
                            header.write_text(_cpp_header_content(header.name), encoding="utf-8")
                        else:
                            dest.write_text(_file_content(tech, name, template_id), encoding="utf-8")
                    except OSError as exc:
                        try:
                            c.set_text("new_file.error", f"Could not create file: {exc}")
                        except Exception:
                            pass
                        return
                    try:
                        c.close_window("new-file")
                    except Exception:
                        pass

                _deferred(_do_create_file)

            elif item_action in ("dialog.file.cancel", "dialog.file.confirm"):
                _deferred(lambda: c.close_window("open-file"))

            elif item_action == "wizard.cancel":
                _deferred(lambda: c.close_window("new-project"))

            elif item_action == "wizard.nav.up":
                current = nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _wizard_navigate(c, p))

            elif item_action == "wizard.nav.home":
                _deferred(lambda: _wizard_navigate(c, str(Path.home())))

            elif item_action == "wizard.create":
                import datetime
                tech_epa    = wizard_state.get("tech_epa",    True)
                tech_cpp    = wizard_state.get("tech_cpp",    True)
                tech_python = wizard_state.get("tech_python", True)
                project_name = wizard_state.get("project_name", "").strip()
                save_dir    = nav_state.get("path", str(Path.home()))

                def _do_create():
                    if not project_name:
                        try:
                            c.set_text("wizard.error", "Project name cannot be empty.")
                        except Exception:
                            pass
                        return

                    if not (tech_epa or tech_cpp or tech_python):
                        try:
                            c.set_text("wizard.error", "Select at least one technology.")
                        except Exception:
                            pass
                        return

                    project_root = Path(save_dir) / project_name
                    try:
                        (project_root / ".elaraproject").mkdir(parents=True, exist_ok=True)
                        if tech_epa:
                            (project_root / "epa").mkdir(exist_ok=True)
                        if tech_cpp:
                            (project_root / "cpp").mkdir(exist_ok=True)
                        if tech_python:
                            (project_root / "python").mkdir(exist_ok=True)
                        (project_root / "build").mkdir(exist_ok=True)
                        if tech_cpp:
                            (project_root / "build" / "cpp").mkdir(exist_ok=True)
                        if tech_epa:
                            (project_root / "build" / "epa").mkdir(exist_ok=True)
                        techs = [t for t, v in [("epa", tech_epa), ("cpp", tech_cpp), ("python", tech_python)] if v]
                        (project_root / ".elaraproject" / "project.json").write_text(
                            json.dumps({
                                "name": project_name,
                                "technologies": techs,
                                "created": datetime.datetime.utcnow().isoformat() + "Z",
                            }, indent=2),
                            encoding="utf-8",
                        )
                        (project_root / ".elaraproject" / "bookmarks.json").write_text("[]", encoding="utf-8")
                        (project_root / ".elaraproject" / "breakpoints.json").write_text("[]", encoding="utf-8")
                    except OSError as exc:
                        try:
                            c.set_text("wizard.error", f"Could not create project: {exc}")
                        except Exception:
                            pass
                        return

                    _open_project(c, project_root)
                    try:
                        c.close_window("new-project")
                    except Exception:
                        pass

                _deferred(_do_create)

            elif item_action and item_action.startswith("tab.close."):
                tab_widget_id = item_action[len("tab.close."):]
                print(json.dumps({"tab_closed": tab_widget_id}, indent=2), flush=True)

        if action not in ("mouseMove", "mouseDown", "mouseUp"):
            print(json.dumps({"ui.event": params}, indent=2), flush=True)
        return {"received": True}

    builder = build_document()
    _init_editor_state()
    document_json = builder.to_json(indent=2)
    artifact_root = Path(__file__).resolve().parent / "artifacts"
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(document_json, encoding="utf-8")
    worker = None
    try:
        with ElaraUiRpcClient(args.host, args.port) as client:
            client_ref["client"] = client
            client.set_find_widget_artifact_root(str(artifact_root))
            if args.event_log:
                client.set_event_log(args.event_log)
            client.add_handler("ui.event", on_ui_event)
            load_result = client.load_document(builder)
            print(json.dumps(load_result, indent=2))
            try:
                client.call("ui.setVisible", {"target": "editor.tabs", "visible": False})
            except Exception:
                pass
            if not app_state.get("project_root"):
                try:
                    client.call("ui.setVisible", {"target": "nav.tree", "visible": False})
                    client.call("ui.setVisible", {"target": "app.toolbar", "visible": False})
                except Exception:
                    pass
            for tab_id in editor_state:
                _refresh_e_tab(client, tab_id)
            if app_state.get("active_editor_tab"):
                _refresh_debug_sidebars(client, app_state["active_editor_tab"])
            snapshot_sections = builder.snapshot_client_sections() if hasattr(builder, "snapshot_client_sections") else {}
            dumper = UiSnapshotDumper(client, client_sections=snapshot_sections)
            if args.snapshot:
                snapshot = dumper.snapshot()
                print(json.dumps(snapshot, indent=2, ensure_ascii=False))
            if args.dump_snapshot:
                path = dumper.dump(args.snapshot_out)
                print(json.dumps({"snapshot_written": str(path)}, indent=2), flush=True)
            if not args.no_events:
                for action in ("clicked", "keysTyped", "textChanged", "valueChanged", "keyDown", "keyUp", "action"):
                    client.enable_event(action)
            if args.once:
                return
            if not args.no_worker:
                try:
                    worker = start_background_worker()
                    print(json.dumps({"multi_cpu_worker": worker.snapshot()}, indent=2), flush=True)
                except RuntimeError as exc:
                    print(json.dumps({"multi_cpu_worker_disabled": str(exc)}, indent=2), flush=True)
            if args.repl:
                repl = ElaraUiRepl(args.host, args.port, client_sections=snapshot_sections, default_snapshot_path=args.snapshot_out)
                # Re-use the already-connected client so the UI event handler remains wired into this process.
                repl.client = client
                repl.dumper = UiSnapshotDumper(client, client_sections=snapshot_sections)
                repl._running = True
                print("Integrated Elara UI REPL ready. Type 'help' for commands.", flush=True)
                while repl._running:
                    try:
                        line = input("elara-ui> ")
                    except (EOFError, KeyboardInterrupt):
                        print()
                        break
                    repl.execute_line(line)
                return
            print("Connected to Elara UI RPC head. Press Ctrl+C to exit.", flush=True)
            while True:
                time.sleep(0.25)
    except KeyboardInterrupt:
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
        return
    except ElaraUiRpcError as exc:
        raise SystemExit(str(exc))


    finally:
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass


if __name__ == "__main__":
    main()

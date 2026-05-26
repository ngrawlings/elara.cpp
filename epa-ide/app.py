import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder
from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError
from elara_ui.snapshot_dumper import UiSnapshotDumper
from elara_ui.repl_client import ElaraUiRepl

from ai_rpc import AiRpcServer, IdeBindings

# Elara Versioning — add versioning directory to path and import.
try:
    _ev_dir = Path(__file__).resolve().parent / "elara_versioning"
    if str(_ev_dir) not in sys.path:
        sys.path.insert(0, str(_ev_dir))
    from elara_versioning.evmanager_core import ProjectRepo, PROJECT_DIR_NAME as EV_PROJECT_DIR
    _EV_AVAILABLE = True
except Exception:
    _EV_AVAILABLE = False
    ProjectRepo = None
    EV_PROJECT_DIR = ".project"


INITIAL_E_TABS = []

AI_MODELS = [
    {"id": "claude-sonnet-4-6",       "label": "Claude Sonnet 4.6"},
    {"id": "claude-haiku-4-5-20251001","label": "Claude Haiku 4.5"},
    {"id": "claude-opus-4-7",          "label": "Claude Opus 4.7"},
]

ANTHROPIC_SYSTEM_PROMPT = (
    "You are the Elara Core AI assistant, embedded in the EPA-IDE development environment.\n\n"
    "You specialise in:\n"
    "- The **E language**: a parallel worker-based language that compiles to EPA bytecode.\n"
    "- **EPA (Elara Parallel Assembly)**: isolated worker address spaces, typed ingress packets, "
    "GHS (Global Heap State) memory transfer, and signal mailboxes.\n"
    "- The **Elara kernel**: scheduling, worker lifecycle, ingress routing, and kernel/host/far signal paths.\n"
    "- **C++ host integration**: bridging the EPA runtime with native host code.\n"
    "- **Python tooling**: scripts and agents working with the EPA build system.\n\n"
    "When analysing code focus on: worker definitions, ingress types, signal paths, kernel coordination, "
    "GHS layout, type declarations, and local-arena vs register usage.\n\n"
    "Be concise. Use fenced code blocks for all code examples. Prefer EPA/E terminology."
)


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

    # Toolbar — E and EPA only
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

    # Content area: editors in col 0, debug memory panel in col 1 (starts hidden at 0 width)
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


def _build_kernel_row_widgets(ui: UiDocumentBuilder, tab_id: str, kernel_name: str):
    row_id = f"nav.debug.kernel.{tab_id}"
    ui.create_grid(row_id)
    ui.add_grid_column_exact(row_id, 6)    # col 0: left indent
    ui.add_grid_column_fill(row_id)        # col 1: name (row 0) / worker combo (row 1)
    ui.add_grid_column_exact(row_id, 20)   # col 2: load indicator
    ui.add_grid_column_exact(row_id, 20)   # col 3: run indicator
    ui.add_grid_column_exact(row_id, 12)   # col 4: spacer before queue
    ui.add_grid_column_exact(row_id, 72)   # col 5: queue badge "total / worker"
    ui.add_grid_column_exact(row_id, 32)   # col 6: ▶  (row 1)
    ui.add_grid_column_exact(row_id, 32)   # col 7: ▶| (row 1)
    ui.add_grid_column_exact(row_id, 4)    # col 8: right margin
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
    ui.create_button(f"{row_id}.run",  "▶",  f"debug.kernel.run.{tab_id}")
    ui.set_property_number(f"{row_id}.run",  "font_size", 11)
    ui.create_button(f"{row_id}.step", "▶|", f"debug.kernel.step.{tab_id}")
    ui.set_property_number(f"{row_id}.step", "font_size", 11)
    ui.place_grid_child(row_id, f"{row_id}.load_ind", 2, 0)
    ui.place_grid_child(row_id, f"{row_id}.run_ind",  3, 0)
    # row 0: name + queue
    ui.place_grid_child(row_id, f"{row_id}.name",   1, 0)
    ui.place_grid_child(row_id, f"{row_id}.queue",  5, 0)
    # row 1: worker combo + buttons
    ui.place_grid_child(row_id, f"{row_id}.worker", 1, 1, 3, 1)
    ui.place_grid_child(row_id, f"{row_id}.run",    6, 1)
    ui.place_grid_child(row_id, f"{row_id}.step",   7, 1)


def build_document():
    ide_state = _current_layout_state()
    layout_state = ide_state.get("layout", {}) if isinstance(ide_state, dict) else {}
    window_state = ide_state.get("window", {}) if isinstance(ide_state, dict) else {}
    use_system_window_header = _use_system_window_header(ide_state)
    right_panel_visible = _right_panel_visible(ide_state)
    bottom_panel_visible = _bottom_panel_visible(ide_state)
    nav_width = _layout_value(layout_state.get("nav_width"), 220)
    ai_width = _layout_value(layout_state.get("ai_width"), 320)
    bottom_height = _layout_value(layout_state.get("bottom_height"), 220)
    window_width = _window_value(window_state.get("width"), 1080)
    window_height = _window_value(window_state.get("height"), 760, minimum=480)

    ui = UiDocumentBuilder()
    ui.create_window("EpaIde", window_width, window_height, "org.elara.ui.epa-ide")
    ui.set_window_property("use_system_header", use_system_window_header)
    ui.set_theme_mode("dark")
    ui.create_grid("app.shell")
    ui.add_grid_column_exact("app.shell", 56)
    ui.add_grid_column_exact("app.shell", nav_width)
    ui.add_grid_column_weighted_fill("app.shell", 3)
    ui.add_grid_column_exact("app.shell", ai_width if right_panel_visible else 0)
    ui.set_grid_column_border_resizable("app.shell", 1, True)
    ui.set_grid_column_border_resizable("app.shell", 2, True)
    ui.add_grid_row_exact("app.shell", 32)
    ui.add_grid_row_fill("app.shell")
    ui.set_root_content("app.shell")

    ui.create_grid("app.center")
    ui.add_grid_column_fill("app.center")
    ui.add_grid_row_fill("app.center")
    ui.add_grid_row_exact("app.center", bottom_height if bottom_panel_visible else 0)
    ui.set_grid_row_border_resizable("app.center", 0, True)
    ui.create_menu_bar("app.menu")
    ui.set_property_number("app.menu", "font_size", 14)
    ui.set_property_bool("app.menu", "custom_chrome", not use_system_window_header)
    ui.set_property_string("app.menu", "window_title", "EPA-IDE")
    ui.add_menu_bar_button("app.menu", "theme_toggle", "◑", "app.toggle_theme")
    ui.add_menu_bar_button("app.menu", "right_panel_toggle", "◨", "app.toggle_right_panel")
    ui.add_menu_bar_button("app.menu", "bottom_panel_toggle", "▤", "app.toggle_bottom_panel")
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
                {"id": "view.appearance.sidebar", "label": "Toggle &Sidebar", "shortcut": "Ctrl+B"},
                {"id": "view.appearance.toggle_window_header", "label": "Toggle Custom Window &Header"}
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
    ui.set_property_bool("nav.tree", "visible", False)
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
    ui.set_property_bool("editor.tabs", "visible", False)
    for tab_id, title, source_text in INITIAL_E_TABS:
        _create_e_tab(ui, tab_id, title, source_text)

    # ── AI dialogue panel ─────────────────────────────────────────────────
    ui.create_grid("ai.panel")
    ui.add_grid_column_fill("ai.panel")
    ui.add_grid_row_exact("ai.panel", 34)            # 0  header bar
    ui.add_grid_row_weighted_fill("ai.panel", 1)     # 1  history  (resizable)
    ui.add_grid_row_exact("ai.panel", 26)            # 2  context toggles
    ui.add_grid_row_exact("ai.panel", 82)            # 3  message input
    ui.add_grid_row_exact("ai.panel", 34)            # 4  action buttons
    ui.set_grid_row_border_resizable("ai.panel", 1, True)

    # Header: "Elara Core" label + model combo
    ui.create_grid("ai.header")
    ui.add_grid_column_exact("ai.header", 8)         # left pad
    ui.add_grid_column_fill("ai.header")             # title
    ui.add_grid_column_exact("ai.header", 148)       # model combo
    ui.add_grid_column_exact("ai.header", 4)         # right pad
    ui.add_grid_row_fill("ai.header")
    ui.create_label("ai.title", "Elara Core", 13)
    ui.create_combo_box("ai.model", AI_MODELS, selected_id="claude-sonnet-4-6")
    ui.set_property_number("ai.model", "font_size", 12)
    ui.place_grid_child("ai.header", "ai.title", 1, 0)
    ui.place_grid_child("ai.header", "ai.model", 2, 0)

    # History area
    ui.create_chat_dialog("ai.history")

    # Context toggle row
    ui.create_grid("ai.ctx_row")
    ui.add_grid_column_exact("ai.ctx_row", 8)        # left pad
    ui.add_grid_column_exact("ai.ctx_row", 58)       # "Context:" label
    ui.add_grid_column_weighted_fill("ai.ctx_row", 1)  # File
    ui.add_grid_column_weighted_fill("ai.ctx_row", 1)  # Project
    ui.add_grid_column_weighted_fill("ai.ctx_row", 1)  # Selection
    ui.add_grid_column_exact("ai.ctx_row", 4)        # right pad
    ui.add_grid_row_fill("ai.ctx_row")
    ui.create_label("ai.ctx_label", "Context:", 11)
    ui.create_checkbox("ai.ctx.file",      "File",      True)
    ui.create_checkbox("ai.ctx.project",   "Project",   False)
    ui.create_checkbox("ai.ctx.selection", "Selection", False)
    ui.set_property_number("ai.ctx.file",      "font_size", 11)
    ui.set_property_number("ai.ctx.project",   "font_size", 11)
    ui.set_property_number("ai.ctx.selection", "font_size", 11)
    ui.place_grid_child("ai.ctx_row", "ai.ctx_label",       1, 0)
    ui.place_grid_child("ai.ctx_row", "ai.ctx.file",        2, 0)
    ui.place_grid_child("ai.ctx_row", "ai.ctx.project",     3, 0)
    ui.place_grid_child("ai.ctx_row", "ai.ctx.selection",   4, 0)

    # Message input
    ui.create_rich_text_edit("ai.input", "")
    ui.set_property_number("ai.input", "font_size", 13)

    # Action row: New Chat (left) | Stop (hidden) + Send (right)
    ui.create_grid("ai.actions")
    ui.add_grid_column_exact("ai.actions", 8)        # left pad
    ui.add_grid_column_exact("ai.actions", 76)       # New Chat
    ui.add_grid_column_fill("ai.actions")            # spacer
    ui.add_grid_column_exact("ai.actions", 60)       # Stop (hidden)
    ui.add_grid_column_exact("ai.actions", 6)        # gap
    ui.add_grid_column_exact("ai.actions", 70)       # Send
    ui.add_grid_column_exact("ai.actions", 8)        # right pad
    ui.add_grid_row_fill("ai.actions")
    ui.create_button("ai.new_chat", "New Chat", "ai.new_chat")
    ui.create_button("ai.stop",     "■ Stop",   "ai.stop")
    ui.create_button("ai.send",     "Send  →",  "ai.send")
    ui.set_property_bool("ai.stop", "visible", False)
    ui.set_property_number("ai.new_chat", "font_size", 12)
    ui.set_property_number("ai.stop",     "font_size", 12)
    ui.set_property_number("ai.send",     "font_size", 12)
    ui.place_grid_child("ai.actions", "ai.new_chat", 1, 0)
    ui.place_grid_child("ai.actions", "ai.stop",     3, 0)
    ui.place_grid_child("ai.actions", "ai.send",     5, 0)

    # Assemble panel
    ui.place_grid_child("ai.panel", "ai.header",  0, 0)
    ui.place_grid_child("ai.panel", "ai.history", 0, 1)
    ui.place_grid_child("ai.panel", "ai.ctx_row", 0, 2)
    ui.place_grid_child("ai.panel", "ai.input",   0, 3)
    ui.place_grid_child("ai.panel", "ai.actions", 0, 4)

    ui.create_toolbar("app.toolbar", orientation="vertical")
    ui.set_property_number("app.toolbar", "font_size", 11)
    ui.set_property_number("app.toolbar", "item_padding_x", 6)
    ui.set_property_number("app.toolbar", "item_padding_y", 10)
    ui.set_property_number("app.toolbar", "item_spacing", 2)
    for item in _project_toolbar_items(False):
        if item.get("separator"):
            ui.add_toolbar_separator("app.toolbar")
        else:
            ui.add_toolbar_item(
                "app.toolbar",
                item["id"],
                item["text"],
                enabled=item.get("enabled", True),
            )
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
    ui.create_grid("nav.panel")
    ui.add_grid_column_fill("nav.panel")
    ui.add_grid_row_exact("nav.panel", 28)
    ui.add_grid_row_fill("nav.panel")

    ui.create_grid("nav.tree_toolbar")
    ui.add_grid_column_fill("nav.tree_toolbar")
    ui.add_grid_column_exact("nav.tree_toolbar", 28)
    ui.add_grid_row_fill("nav.tree_toolbar")
    ui.create_button("nav.refresh", "↺", "nav.refresh")
    ui.place_grid_child("nav.tree_toolbar", "nav.refresh", 1, 0)

    ui.place_grid_child("nav.panel", "nav.tree_toolbar", 0, 0)
    ui.place_grid_child("nav.panel", "nav.no_project", 0, 1)
    ui.place_grid_child("nav.panel", "nav.tree", 0, 1)

    # ── Search panel ──────────────────────────────────────────────────────
    ui.create_grid("nav.search_panel")
    ui.add_grid_column_fill("nav.search_panel")
    ui.add_grid_row_exact("nav.search_panel", 28)   # header
    ui.add_grid_row_exact("nav.search_panel", 34)   # search input
    ui.add_grid_row_fill("nav.search_panel")        # results

    ui.create_grid("nav.search_header")
    ui.add_grid_column_exact("nav.search_header", 8)
    ui.add_grid_column_fill("nav.search_header")
    ui.add_grid_row_fill("nav.search_header")
    ui.create_label("nav.search_title", "SEARCH", 11)
    ui.place_grid_child("nav.search_header", "nav.search_title", 1, 0)

    ui.create_grid("nav.search_input_row")
    ui.add_grid_column_exact("nav.search_input_row", 6)
    ui.add_grid_column_fill("nav.search_input_row")
    ui.add_grid_column_exact("nav.search_input_row", 6)
    ui.add_grid_row_fill("nav.search_input_row")
    ui.create_text_input("nav.search.input", "")
    ui.set_property_string("nav.search.input", "placeholder", "Search files…")
    ui.set_property_number("nav.search.input", "font_size", 13)
    ui.place_grid_child("nav.search_input_row", "nav.search.input", 1, 0)

    ui.create_list_view("nav.search.results")
    ui.set_property_number("nav.search.results", "font_size", 12)
    ui.set_section_json("nav.search.results", "items", [])

    ui.place_grid_child("nav.search_panel", "nav.search_header",    0, 0)
    ui.place_grid_child("nav.search_panel", "nav.search_input_row", 0, 1)
    ui.place_grid_child("nav.search_panel", "nav.search.results",   0, 2)
    ui.set_property_bool("nav.search_panel", "visible", False)

    # ── Versioning panel (Git + EV tabs) ─────────────────────────────────
    # -- Git tab content --
    ui.create_grid("nav.git_panel")
    ui.add_grid_column_fill("nav.git_panel")
    ui.add_grid_row_exact("nav.git_panel", 24)    # status summary
    ui.add_grid_row_fill("nav.git_panel")         # changed files list
    ui.add_grid_row_exact("nav.git_panel", 60)    # commit message
    ui.add_grid_row_exact("nav.git_panel", 34)    # buttons row

    ui.create_grid("nav.git_status_row")
    ui.add_grid_column_exact("nav.git_status_row", 6)
    ui.add_grid_column_fill("nav.git_status_row")
    ui.add_grid_column_exact("nav.git_status_row", 28)
    ui.add_grid_row_fill("nav.git_status_row")
    ui.create_label("nav.repo.status", "No changes", 11)
    ui.set_property_bool("nav.repo.status", "enabled", False)
    ui.create_button("nav.repo.refresh", "↺", "repo.refresh")
    ui.place_grid_child("nav.git_status_row", "nav.repo.status",  1, 0)
    ui.place_grid_child("nav.git_status_row", "nav.repo.refresh", 2, 0)

    ui.create_list_view("nav.repo.changes")
    ui.set_property_number("nav.repo.changes", "font_size", 12)
    ui.set_section_json("nav.repo.changes", "items", [])

    ui.create_text_input("nav.repo.commit_msg", "")
    ui.set_property_string("nav.repo.commit_msg", "placeholder", "Commit message…")
    ui.set_property_number("nav.repo.commit_msg", "font_size", 12)

    ui.create_grid("nav.repo_buttons")
    ui.add_grid_column_exact("nav.repo_buttons", 6)
    ui.add_grid_column_weighted_fill("nav.repo_buttons", 1)
    ui.add_grid_column_exact("nav.repo_buttons", 6)
    ui.add_grid_column_weighted_fill("nav.repo_buttons", 1)
    ui.add_grid_column_exact("nav.repo_buttons", 6)
    ui.add_grid_row_fill("nav.repo_buttons")
    ui.create_button("nav.repo.stage_all", "Stage All", "repo.stage_all")
    ui.create_button("nav.repo.commit",    "Commit",    "repo.commit")
    ui.set_property_number("nav.repo.stage_all", "font_size", 12)
    ui.set_property_number("nav.repo.commit",    "font_size", 12)
    ui.place_grid_child("nav.repo_buttons", "nav.repo.stage_all", 1, 0)
    ui.place_grid_child("nav.repo_buttons", "nav.repo.commit",    3, 0)

    ui.place_grid_child("nav.git_panel", "nav.git_status_row",  0, 0)
    ui.place_grid_child("nav.git_panel", "nav.repo.changes",    0, 1)
    ui.place_grid_child("nav.git_panel", "nav.repo.commit_msg", 0, 2)
    ui.place_grid_child("nav.git_panel", "nav.repo_buttons",    0, 3)

    # -- Elara Versioning tab content --
    # ── EV repo content (shown when repo exists) ─────────────────────────
    ui.create_grid("nav.ev_repo_content")
    ui.add_grid_column_fill("nav.ev_repo_content")
    ui.add_grid_row_exact("nav.ev_repo_content", 28)    # branch row
    ui.add_grid_row_exact("nav.ev_repo_content", 24)    # status summary
    ui.add_grid_row_fill("nav.ev_repo_content")          # changed files list
    ui.add_grid_row_exact("nav.ev_repo_content", 60)    # commit message
    ui.add_grid_row_exact("nav.ev_repo_content", 34)    # buttons row

    ui.create_grid("nav.ev_branch_row")
    ui.add_grid_column_exact("nav.ev_branch_row", 6)
    ui.add_grid_column_fill("nav.ev_branch_row")
    ui.add_grid_column_exact("nav.ev_branch_row", 50)
    ui.add_grid_column_exact("nav.ev_branch_row", 4)
    ui.add_grid_row_fill("nav.ev_branch_row")
    ui.create_label("nav.ev.branch_label", "No repo", 11)
    ui.set_property_bool("nav.ev.branch_label", "enabled", False)
    ui.create_button("nav.ev.refresh", "↺", "ev.refresh")
    ui.place_grid_child("nav.ev_branch_row", "nav.ev.branch_label", 1, 0)
    ui.place_grid_child("nav.ev_branch_row", "nav.ev.refresh",      2, 0)

    ui.create_label("nav.ev.status", "", 11)
    ui.set_property_bool("nav.ev.status", "enabled", False)

    ui.create_list_view("nav.ev.changes")
    ui.set_property_number("nav.ev.changes", "font_size", 12)
    ui.set_section_json("nav.ev.changes", "items", [])

    ui.create_text_input("nav.ev.commit_msg", "")
    ui.set_property_string("nav.ev.commit_msg", "placeholder", "Commit message…")
    ui.set_property_number("nav.ev.commit_msg", "font_size", 12)

    ui.create_grid("nav.ev_buttons")
    ui.add_grid_column_exact("nav.ev_buttons", 4)
    ui.add_grid_column_weighted_fill("nav.ev_buttons", 1)
    ui.add_grid_column_exact("nav.ev_buttons", 4)
    ui.add_grid_column_weighted_fill("nav.ev_buttons", 1)
    ui.add_grid_column_exact("nav.ev_buttons", 4)
    ui.add_grid_column_weighted_fill("nav.ev_buttons", 1)
    ui.add_grid_column_exact("nav.ev_buttons", 4)
    ui.add_grid_row_fill("nav.ev_buttons")
    ui.create_button("nav.ev.commit", "Commit", "ev.commit")
    ui.create_button("nav.ev.push",   "Push",   "ev.push")
    ui.create_button("nav.ev.pull",   "Pull",   "ev.pull")
    ui.set_property_number("nav.ev.commit", "font_size", 11)
    ui.set_property_number("nav.ev.push",   "font_size", 11)
    ui.set_property_number("nav.ev.pull",   "font_size", 11)
    ui.place_grid_child("nav.ev_buttons", "nav.ev.commit", 1, 0)
    ui.place_grid_child("nav.ev_buttons", "nav.ev.push",   3, 0)
    ui.place_grid_child("nav.ev_buttons", "nav.ev.pull",   5, 0)

    ui.place_grid_child("nav.ev_repo_content", "nav.ev_branch_row", 0, 0)
    ui.place_grid_child("nav.ev_repo_content", "nav.ev.status",     0, 1)
    ui.place_grid_child("nav.ev_repo_content", "nav.ev.changes",    0, 2)
    ui.place_grid_child("nav.ev_repo_content", "nav.ev.commit_msg", 0, 3)
    ui.place_grid_child("nav.ev_repo_content", "nav.ev_buttons",    0, 4)

    # ── EV tab setup form (shown when no repo exists) ─────────────────────
    ui.create_grid("nav.ev_tab_setup_form")
    ui.add_grid_column_exact("nav.ev_tab_setup_form", 10)
    ui.add_grid_column_fill("nav.ev_tab_setup_form")
    ui.add_grid_column_exact("nav.ev_tab_setup_form", 10)
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 13)   # Project name label
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 26)   # Project name input
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 13)   # Sync server label
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 26)   # Sync server input
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 13)   # Remote root label
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 26)   # Remote root input
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 13)   # Branch label
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 26)   # Branch input
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 12)   # spacer
    ui.add_grid_row_exact("nav.ev_tab_setup_form", 90)   # about note

    _EV_ABOUT_NOTE = (
        "Version control for high-security networks.\n"
        "Designed for environments where only text-\n"
        "based connections are permitted — air-gapped\n"
        "labs, classified networks, and sites where\n"
        "standard internet access is not available.\n"
        "Simple by design. Ultra security friendly."
    )

    ui.create_label("nav.ev_tab_setup.lbl_name",   "Project name", 11)
    ui.create_label("nav.ev_tab_setup.lbl_server", "Sync server",  11)
    ui.create_label("nav.ev_tab_setup.lbl_root",   "Remote root",  11)
    ui.create_label("nav.ev_tab_setup.lbl_branch", "Branch",       11)
    ui.create_label("nav.ev_tab_setup.note", _EV_ABOUT_NOTE, 10)
    ui.set_property_bool("nav.ev_tab_setup.note", "enabled", False)

    ui.create_text_input("nav.ev_tab_setup.name",        "")
    ui.create_text_input("nav.ev_tab_setup.server",      "")
    ui.create_text_input("nav.ev_tab_setup.remote_root", "")
    ui.create_text_input("nav.ev_tab_setup.branch",      "main")

    ui.set_property_string("nav.ev_tab_setup.name",        "placeholder", "My Project")
    ui.set_property_string("nav.ev_tab_setup.server",      "placeholder", "https://sync-server.example.com")
    ui.set_property_string("nav.ev_tab_setup.remote_root", "placeholder", "/projects/myproject")
    ui.set_property_string("nav.ev_tab_setup.branch",      "placeholder", "main")

    ui.set_property_number("nav.ev_tab_setup.name",        "font_size", 12)
    ui.set_property_number("nav.ev_tab_setup.server",      "font_size", 12)
    ui.set_property_number("nav.ev_tab_setup.remote_root", "font_size", 12)
    ui.set_property_number("nav.ev_tab_setup.branch",      "font_size", 12)

    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.lbl_name",    1, 0)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.name",         1, 1)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.lbl_server",  1, 3)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.server",       1, 4)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.lbl_root",    1, 6)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.remote_root",  1, 7)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.lbl_branch",  1, 9)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.branch",       1, 10)
    ui.place_grid_child("nav.ev_tab_setup_form", "nav.ev_tab_setup.note",         1, 12)

    ui.create_grid("nav.ev_tab_setup_panel")
    ui.add_grid_column_fill("nav.ev_tab_setup_panel")
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 14)   # top padding
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 30)   # description label
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 10)   # spacer
    ui.add_grid_row_fill("nav.ev_tab_setup_panel")         # form
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 10)   # spacer
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 28)   # init button
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 22)   # error label
    ui.add_grid_row_exact("nav.ev_tab_setup_panel", 10)   # bottom padding

    ui.create_label("nav.ev_tab_setup.desc",     "Initialize Elara Versioning\nfor this project.", 11)
    ui.create_button("nav.ev_tab_setup.init_btn", "Initialize Repo", "ev.tab.setup.init")
    ui.set_property_number("nav.ev_tab_setup.init_btn", "font_size", 12)
    ui.create_label("nav.ev_tab_setup.error", "", 11)

    ui.place_grid_child("nav.ev_tab_setup_panel", "nav.ev_tab_setup.desc",     0, 1)
    ui.place_grid_child("nav.ev_tab_setup_panel", "nav.ev_tab_setup_form",     0, 3)
    ui.place_grid_child("nav.ev_tab_setup_panel", "nav.ev_tab_setup.init_btn", 0, 5)
    ui.place_grid_child("nav.ev_tab_setup_panel", "nav.ev_tab_setup.error",    0, 6)

    # ── EV panel (stacks repo content and setup form) ─────────────────────
    ui.create_grid("nav.ev_panel")
    ui.add_grid_column_fill("nav.ev_panel")
    ui.add_grid_row_fill("nav.ev_panel")
    ui.place_grid_child("nav.ev_panel", "nav.ev_repo_content",    0, 0)
    ui.place_grid_child("nav.ev_panel", "nav.ev_tab_setup_panel", 0, 0)

    # -- Tabs wrapper --
    ui.create_tabs("nav.repo_panel")
    ui.add_tab("nav.repo_panel", "Git", "nav.git_panel")
    ui.add_tab("nav.repo_panel", "EV",  "nav.ev_panel")
    ui.set_property_bool("nav.repo_panel", "visible", False)

    # ── Issues panel ─────────────────────────────────────────────────────
    ui.create_grid("nav.issues_panel")
    ui.add_grid_column_fill("nav.issues_panel")
    ui.add_grid_row_exact("nav.issues_panel", 28)    # header
    ui.add_grid_row_fill("nav.issues_panel")         # content area (stacked)

    ui.create_grid("nav.issues_header")
    ui.add_grid_column_exact("nav.issues_header", 8)
    ui.add_grid_column_fill("nav.issues_header")
    ui.add_grid_column_exact("nav.issues_header", 28)
    ui.add_grid_row_fill("nav.issues_header")
    ui.create_label("nav.issues_title", "ISSUES", 11)
    ui.create_button("nav.issues.refresh", "↺", "issues.refresh")
    ui.place_grid_child("nav.issues_header", "nav.issues_title",   1, 0)
    ui.place_grid_child("nav.issues_header", "nav.issues.refresh", 2, 0)

    # ── Bug list content ──────────────────────────────────────────────────
    ui.create_grid("nav.ev_issues_content")
    ui.add_grid_column_fill("nav.ev_issues_content")
    ui.add_grid_row_exact("nav.ev_issues_content", 26)   # filter row
    ui.add_grid_row_exact("nav.ev_issues_content", 34)   # input row
    ui.add_grid_row_fill("nav.ev_issues_content")         # list
    ui.add_grid_row_exact("nav.ev_issues_content", 34)   # action buttons

    ui.create_grid("nav.issues_filter_row")
    ui.add_grid_column_exact("nav.issues_filter_row", 6)
    ui.add_grid_column_weighted_fill("nav.issues_filter_row", 1)
    ui.add_grid_column_exact("nav.issues_filter_row", 4)
    ui.add_grid_column_weighted_fill("nav.issues_filter_row", 1)
    ui.add_grid_column_exact("nav.issues_filter_row", 4)
    ui.add_grid_column_weighted_fill("nav.issues_filter_row", 1)
    ui.add_grid_column_exact("nav.issues_filter_row", 6)
    ui.add_grid_row_fill("nav.issues_filter_row")
    ui.create_button("nav.issues.filter_all",    "All",    "issues.filter.all")
    ui.create_button("nav.issues.filter_open",   "Open",   "issues.filter.open")
    ui.create_button("nav.issues.filter_closed", "Closed", "issues.filter.closed")
    ui.set_property_number("nav.issues.filter_all",    "font_size", 11)
    ui.set_property_number("nav.issues.filter_open",   "font_size", 11)
    ui.set_property_number("nav.issues.filter_closed", "font_size", 11)
    ui.place_grid_child("nav.issues_filter_row", "nav.issues.filter_all",    1, 0)
    ui.place_grid_child("nav.issues_filter_row", "nav.issues.filter_open",   3, 0)
    ui.place_grid_child("nav.issues_filter_row", "nav.issues.filter_closed", 5, 0)

    ui.create_grid("nav.issues_input_row")
    ui.add_grid_column_exact("nav.issues_input_row", 6)
    ui.add_grid_column_fill("nav.issues_input_row")
    ui.add_grid_column_exact("nav.issues_input_row", 34)
    ui.add_grid_column_exact("nav.issues_input_row", 6)
    ui.add_grid_row_fill("nav.issues_input_row")
    ui.create_text_input("nav.issues.new_title", "")
    ui.set_property_string("nav.issues.new_title", "placeholder", "New bug title…")
    ui.set_property_number("nav.issues.new_title", "font_size", 13)
    ui.create_button("nav.issues.add", "+", "issues.add")
    ui.set_property_number("nav.issues.add", "font_size", 13)
    ui.place_grid_child("nav.issues_input_row", "nav.issues.new_title", 1, 0)
    ui.place_grid_child("nav.issues_input_row", "nav.issues.add",       2, 0)

    ui.create_list_view("nav.issues.list")
    ui.set_property_number("nav.issues.list", "font_size", 12)
    ui.set_section_json("nav.issues.list", "items", [])

    ui.create_grid("nav.issues_buttons")
    ui.add_grid_column_exact("nav.issues_buttons", 6)
    ui.add_grid_column_weighted_fill("nav.issues_buttons", 1)
    ui.add_grid_column_exact("nav.issues_buttons", 6)
    ui.add_grid_column_weighted_fill("nav.issues_buttons", 1)
    ui.add_grid_column_exact("nav.issues_buttons", 6)
    ui.add_grid_row_fill("nav.issues_buttons")
    ui.create_button("nav.issues.close_btn", "Close Bug",  "issues.close")
    ui.create_button("nav.issues.reopen_btn", "Reopen",    "issues.reopen")
    ui.set_property_number("nav.issues.close_btn",  "font_size", 12)
    ui.set_property_number("nav.issues.reopen_btn", "font_size", 12)
    ui.place_grid_child("nav.issues_buttons", "nav.issues.close_btn",  1, 0)
    ui.place_grid_child("nav.issues_buttons", "nav.issues.reopen_btn", 3, 0)

    ui.place_grid_child("nav.ev_issues_content", "nav.issues_filter_row", 0, 0)
    ui.place_grid_child("nav.ev_issues_content", "nav.issues_input_row",  0, 1)
    ui.place_grid_child("nav.ev_issues_content", "nav.issues.list",       0, 2)
    ui.place_grid_child("nav.ev_issues_content", "nav.issues_buttons",    0, 3)

    # ── EV setup form (shown when no repo exists) ─────────────────────────
    ui.create_grid("nav.ev_setup_form")
    ui.add_grid_column_exact("nav.ev_setup_form", 10)
    ui.add_grid_column_fill("nav.ev_setup_form")
    ui.add_grid_column_exact("nav.ev_setup_form", 10)
    ui.add_grid_row_exact("nav.ev_setup_form", 13)   # Project name label
    ui.add_grid_row_exact("nav.ev_setup_form", 26)   # Project name input
    ui.add_grid_row_exact("nav.ev_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_setup_form", 13)   # Sync server label
    ui.add_grid_row_exact("nav.ev_setup_form", 26)   # Sync server input
    ui.add_grid_row_exact("nav.ev_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_setup_form", 13)   # Remote root label
    ui.add_grid_row_exact("nav.ev_setup_form", 26)   # Remote root input
    ui.add_grid_row_exact("nav.ev_setup_form", 8)    # spacer
    ui.add_grid_row_exact("nav.ev_setup_form", 13)   # Branch label
    ui.add_grid_row_exact("nav.ev_setup_form", 26)   # Branch input
    ui.add_grid_row_exact("nav.ev_setup_form", 12)   # spacer
    ui.add_grid_row_exact("nav.ev_setup_form", 90)   # about note

    ui.create_label("nav.ev_setup.lbl_name",   "Project name", 11)
    ui.create_label("nav.ev_setup.lbl_server", "Sync server",  11)
    ui.create_label("nav.ev_setup.lbl_root",   "Remote root",  11)
    ui.create_label("nav.ev_setup.lbl_branch", "Branch",       11)
    ui.create_label("nav.ev_setup.note", _EV_ABOUT_NOTE, 10)
    ui.set_property_bool("nav.ev_setup.note", "enabled", False)

    ui.create_text_input("nav.ev_setup.name",        "")
    ui.create_text_input("nav.ev_setup.server",      "")
    ui.create_text_input("nav.ev_setup.remote_root", "")
    ui.create_text_input("nav.ev_setup.branch",      "main")

    ui.set_property_string("nav.ev_setup.name",        "placeholder", "My Project")
    ui.set_property_string("nav.ev_setup.server",      "placeholder", "https://sync-server.example.com")
    ui.set_property_string("nav.ev_setup.remote_root", "placeholder", "/projects/myproject")
    ui.set_property_string("nav.ev_setup.branch",      "placeholder", "main")

    ui.set_property_number("nav.ev_setup.name",        "font_size", 12)
    ui.set_property_number("nav.ev_setup.server",      "font_size", 12)
    ui.set_property_number("nav.ev_setup.remote_root", "font_size", 12)
    ui.set_property_number("nav.ev_setup.branch",      "font_size", 12)

    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.lbl_name",    1, 0)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.name",         1, 1)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.lbl_server",  1, 3)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.server",       1, 4)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.lbl_root",    1, 6)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.remote_root",  1, 7)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.lbl_branch",  1, 9)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.branch",       1, 10)
    ui.place_grid_child("nav.ev_setup_form", "nav.ev_setup.note",         1, 12)

    ui.create_grid("nav.ev_setup_panel")
    ui.add_grid_column_fill("nav.ev_setup_panel")
    ui.add_grid_row_exact("nav.ev_setup_panel", 14)   # top padding
    ui.add_grid_row_exact("nav.ev_setup_panel", 30)   # description label
    ui.add_grid_row_exact("nav.ev_setup_panel", 10)   # spacer
    ui.add_grid_row_fill("nav.ev_setup_panel")         # form
    ui.add_grid_row_exact("nav.ev_setup_panel", 10)   # spacer
    ui.add_grid_row_exact("nav.ev_setup_panel", 28)   # init button
    ui.add_grid_row_exact("nav.ev_setup_panel", 22)   # error label
    ui.add_grid_row_exact("nav.ev_setup_panel", 10)   # bottom padding

    ui.create_label("nav.ev_setup.desc",     "Initialize Elara Versioning\nfor this project.", 11)
    ui.create_button("nav.ev_setup.init_btn", "Initialize Repo", "ev.setup.init")
    ui.set_property_number("nav.ev_setup.init_btn", "font_size", 12)
    ui.create_label("nav.ev_setup.error", "", 11)

    ui.place_grid_child("nav.ev_setup_panel", "nav.ev_setup.desc",     0, 1)
    ui.place_grid_child("nav.ev_setup_panel", "nav.ev_setup_form",     0, 3)
    ui.place_grid_child("nav.ev_setup_panel", "nav.ev_setup.init_btn", 0, 5)
    ui.place_grid_child("nav.ev_setup_panel", "nav.ev_setup.error",    0, 6)

    ui.place_grid_child("nav.issues_panel", "nav.issues_header",     0, 0)
    ui.place_grid_child("nav.issues_panel", "nav.ev_issues_content", 0, 1)
    ui.place_grid_child("nav.issues_panel", "nav.ev_setup_panel",    0, 1)
    ui.set_property_bool("nav.issues_panel", "visible", False)

    # ── Debug left panel ─────────────────────────────────────────────────
    ui.create_grid("nav.debug_panel")
    ui.add_grid_column_fill("nav.debug_panel")
    ui.add_grid_row_exact("nav.debug_panel", 28)         # 0  header
    ui.add_grid_row_exact("nav.debug_panel", 246)        # 1  ingress designer (fixed)
    ui.add_grid_row_exact("nav.debug_panel", 22)         # 2  kernels section label
    ui.add_grid_row_weighted_fill("nav.debug_panel", 1)  # 3  kernel list
    ui.add_grid_row_exact("nav.debug_panel", 58)         # 4  VM status + controls

    ui.create_grid("nav.debug_header")
    ui.add_grid_column_exact("nav.debug_header", 8)
    ui.add_grid_column_fill("nav.debug_header")
    ui.add_grid_row_fill("nav.debug_header")
    ui.create_label("nav.debug_title", "DEBUG", 11)
    ui.place_grid_child("nav.debug_header", "nav.debug_title", 1, 0)

    # VM control strip at the bottom
    ui.create_grid("nav.debug.vm_controls")
    ui.add_grid_column_exact("nav.debug.vm_controls", 8)
    ui.add_grid_column_fill("nav.debug.vm_controls")
    ui.add_grid_column_exact("nav.debug.vm_controls", 4)
    ui.add_grid_column_exact("nav.debug.vm_controls", 80)
    ui.add_grid_column_exact("nav.debug.vm_controls", 4)
    ui.add_grid_column_exact("nav.debug.vm_controls", 80)
    ui.add_grid_column_exact("nav.debug.vm_controls", 8)
    ui.add_grid_row_exact("nav.debug.vm_controls", 24)
    ui.add_grid_row_exact("nav.debug.vm_controls", 30)
    ui.create_label("nav.debug.vm_status", "●  VM idle", 10)
    ui.set_property_string("nav.debug.vm_status", "foreground_color", "#777777")
    ui.create_button("nav.debug.vm_reset", "▶  Start", "debug.vm.reset")
    ui.set_property_number("nav.debug.vm_reset", "font_size", 11)
    ui.create_button("nav.debug.vm_stop", "■  Stop", "debug.vm.stop")
    ui.set_property_number("nav.debug.vm_stop", "font_size", 11)
    ui.set_property_bool("nav.debug.vm_stop", "enabled", False)
    ui.place_grid_child("nav.debug.vm_controls", "nav.debug.vm_status", 1, 0, 5, 1)
    ui.place_grid_child("nav.debug.vm_controls", "nav.debug.vm_reset",  3, 1)
    ui.place_grid_child("nav.debug.vm_controls", "nav.debug.vm_stop",   5, 1)

    # Ingress designer
    ui.create_grid("nav.debug.ingress")
    ui.add_grid_column_exact("nav.debug.ingress", 8)
    ui.add_grid_column_fill("nav.debug.ingress")
    ui.add_grid_column_exact("nav.debug.ingress", 8)
    ui.add_grid_row_exact("nav.debug.ingress", 22)    # 0  section label
    ui.add_grid_row_exact("nav.debug.ingress", 28)    # 1  kernel / worker row
    ui.add_grid_row_exact("nav.debug.ingress", 28)    # 2  type row
    ui.add_grid_row_exact("nav.debug.ingress", 112)   # 3  profile list (5 × ~22 px)
    ui.add_grid_row_exact("nav.debug.ingress", 28)    # 4  add / queue buttons
    # 22 + 28 + 28 + 112 + 28 = 218 → panel row 1 is 246 for breathing room
    ui.create_label("nav.debug.ingress.title", "INGRESS DESIGNER", 10)
    ui.set_property_bool("nav.debug.ingress.title", "enabled", False)

    # Kernel + worker side-by-side
    ui.create_grid("nav.debug.ingress.kw")
    ui.add_grid_column_fill("nav.debug.ingress.kw")
    ui.add_grid_column_exact("nav.debug.ingress.kw", 4)
    ui.add_grid_column_fill("nav.debug.ingress.kw")
    ui.add_grid_row_fill("nav.debug.ingress.kw")
    ui.create_combo_box("nav.debug.ingress_kernel", items=[], selected_id="")
    ui.create_combo_box("nav.debug.ingress_worker", items=[], selected_id="")
    ui.place_grid_child("nav.debug.ingress.kw", "nav.debug.ingress_kernel", 0, 0)
    ui.place_grid_child("nav.debug.ingress.kw", "nav.debug.ingress_worker", 2, 0)

    ui.create_combo_box("nav.debug.ingress_type", items=[], selected_id="")
    ui.create_list_view("nav.debug.ingress_profiles")

    # Action buttons row: [+ Add Profile] [gap] [> Queue]
    ui.create_grid("nav.debug.ingress.actions")
    ui.add_grid_column_fill("nav.debug.ingress.actions")
    ui.add_grid_column_exact("nav.debug.ingress.actions", 4)
    ui.add_grid_column_exact("nav.debug.ingress.actions", 72)
    ui.add_grid_row_fill("nav.debug.ingress.actions")
    ui.create_button("nav.debug.ingress_add_btn",   "+ Add Profile", "ingress.add_profile")
    ui.create_button("nav.debug.ingress_queue_btn", "> Queue",        "ingress.queue_packet")

    ui.place_grid_child("nav.debug.ingress.actions", "nav.debug.ingress_add_btn",   0, 0)
    ui.place_grid_child("nav.debug.ingress.actions", "nav.debug.ingress_queue_btn", 2, 0)

    ui.place_grid_child("nav.debug.ingress", "nav.debug.ingress.title",    1, 0)
    ui.place_grid_child("nav.debug.ingress", "nav.debug.ingress.kw",       1, 1)
    ui.place_grid_child("nav.debug.ingress", "nav.debug.ingress_type",     1, 2)
    ui.place_grid_child("nav.debug.ingress", "nav.debug.ingress_profiles", 1, 3)
    ui.place_grid_child("nav.debug.ingress", "nav.debug.ingress.actions",  1, 4)

    # Kernels section header
    ui.create_grid("nav.debug.kernels_header")
    ui.add_grid_column_exact("nav.debug.kernels_header", 8)
    ui.add_grid_column_fill("nav.debug.kernels_header")
    ui.add_grid_row_fill("nav.debug.kernels_header")
    ui.create_label("nav.debug.kernels_title", "KERNEL STEP", 10)
    ui.set_property_bool("nav.debug.kernels_title", "enabled", False)
    ui.place_grid_child("nav.debug.kernels_header", "nav.debug.kernels_title", 1, 0)

    # Kernel list (one entry per .e tab / kernel)
    ui.create_list_layout("nav.debug.kernels")
    for tab_id, title, _ in INITIAL_E_TABS:
        kernel_name = Path(title).stem if "." in title else title
        _build_kernel_row_widgets(ui, tab_id, kernel_name)
        ui.place_list_layout_child("nav.debug.kernels", f"nav.debug.kernel.{tab_id}", row_height=52)

    ui.place_grid_child("nav.debug_panel", "nav.debug_header",         0, 0)
    ui.place_grid_child("nav.debug_panel", "nav.debug.ingress",        0, 1)
    ui.place_grid_child("nav.debug_panel", "nav.debug.kernels_header", 0, 2)
    ui.place_grid_child("nav.debug_panel", "nav.debug.kernels",        0, 3)
    ui.place_grid_child("nav.debug_panel", "nav.debug.vm_controls",    0, 4)
    ui.set_property_bool("nav.debug_panel", "visible", False)

    # -- Bottom tools panel -------------------------------------------------
    ui.create_grid("bottom.panel")
    ui.add_grid_column_fill("bottom.panel")
    ui.add_grid_row_exact("bottom.panel", 30)
    ui.add_grid_row_fill("bottom.panel")

    ui.create_toolbar("bottom.toolbar", orientation="horizontal")
    ui.set_property_number("bottom.toolbar", "font_size", 11)
    ui.set_property_number("bottom.toolbar", "item_padding_x", 8)
    ui.set_property_number("bottom.toolbar", "item_padding_y", 5)
    ui.set_property_number("bottom.toolbar", "item_spacing", 2)
    ui.add_toolbar_item("bottom.toolbar", "bottom.build", "Build Output")
    ui.add_toolbar_item("bottom.toolbar", "bottom.terminal", "Terminal")
    ui.add_toolbar_separator("bottom.toolbar")
    ui.add_toolbar_item("bottom.toolbar", "bottom.clear", "Clear")

    ui.create_rich_text_edit(
        "bottom.build_output",
        "Build output will appear here.",
    )
    ui.set_property_number("bottom.build_output", "font_size", 12)
    ui.set_property_bool("bottom.build_output", "read_only", True)

    ui.create_grid("bottom.terminal_panel")
    ui.add_grid_column_fill("bottom.terminal_panel")
    ui.add_grid_column_exact("bottom.terminal_panel", 132)
    ui.set_grid_column_border_resizable("bottom.terminal_panel", 0, True)
    ui.add_grid_row_fill("bottom.terminal_panel")
    ui.add_grid_row_exact("bottom.terminal_panel", 28)
    ui.create_rich_text_edit(
        "bottom.terminal_output",
        "Terminal ready. Open a project to set the working directory.",
    )
    ui.set_property_number("bottom.terminal_output", "font_size", 12)
    ui.set_property_bool("bottom.terminal_output", "read_only", True)
    ui.create_text_input("bottom.terminal_input", "")
    ui.set_property_string("bottom.terminal_input", "placeholder", "command")
    ui.set_property_number("bottom.terminal_input", "font_size", 12)
    ui.create_list_view("bottom.terminal_instances")
    ui.set_property_number("bottom.terminal_instances", "font_size", 11)
    ui.set_section_json("bottom.terminal_instances", "items", [
        {"id": "terminal.1", "label": "Terminal 1"},
    ])
    ui.place_grid_child("bottom.terminal_panel", "bottom.terminal_output", 0, 0)
    ui.place_grid_child("bottom.terminal_panel", "bottom.terminal_input", 0, 1)
    ui.place_grid_child("bottom.terminal_panel", "bottom.terminal_instances", 1, 0, 1, 2)

    ui.set_property_bool("bottom.terminal_panel", "visible", False)
    ui.place_grid_child("bottom.panel", "bottom.toolbar", 0, 0)
    ui.place_grid_child("bottom.panel", "bottom.build_output", 0, 1)
    ui.place_grid_child("bottom.panel", "bottom.terminal_panel", 0, 1)
    ui.set_property_bool("bottom.panel", "visible", bottom_panel_visible)

    ui.place_grid_child("app.shell", "app.menu", 0, 0, 4, 1)
    ui.place_grid_child("app.shell", "app.toolbar", 0, 1)
    ui.place_grid_child("app.shell", "nav.panel",          1, 1)
    ui.place_grid_child("app.shell", "nav.search_panel",  1, 1)
    ui.place_grid_child("app.shell", "nav.repo_panel",    1, 1)
    ui.place_grid_child("app.shell", "nav.issues_panel",  1, 1)
    ui.place_grid_child("app.shell", "nav.debug_panel",   1, 1)
    ui.place_grid_child("app.shell", "app.center", 2, 1)
    ui.place_grid_child("app.center", "editor.welcome", 0, 0)
    ui.place_grid_child("app.center", "editor.tabs", 0, 0)
    ui.place_grid_child("app.center", "bottom.panel", 0, 1)
    ui.place_grid_child("app.shell", "ai.panel", 3, 1)
    ui.set_property_bool("ai.panel", "visible", right_panel_visible)
    return ui


def _ide_state_path() -> Path:
    return Path.home() / ".config" / "epa-ide" / "state.json"


def _load_ide_state() -> dict:
    p = _ide_state_path()
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except Exception:
        return {}


def _save_ide_state(updates: dict):
    p = _ide_state_path()
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        state = _load_ide_state()
        for key, value in updates.items():
            if isinstance(value, dict) and isinstance(state.get(key), dict):
                merged = dict(state[key])
                merged.update(value)
                state[key] = merged
            else:
                state[key] = value
        p.write_text(json.dumps(state, indent=2), encoding="utf-8")
    except Exception:
        pass


def _layout_value(value, fallback: int, minimum: int = 120) -> int:
    try:
        parsed = int(round(float(value)))
    except Exception:
        return fallback
    return parsed if parsed >= minimum else fallback


def _window_value(value, fallback: int, minimum: int = 640) -> int:
    try:
        parsed = int(round(float(value)))
    except Exception:
        return fallback
    return parsed if parsed >= minimum else fallback


def _current_layout_state() -> dict:
    return _load_ide_state()


def _use_system_window_header(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict):
        return bool(ui_state.get("use_system_window_header", False))
    return False


def _right_panel_visible(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict) and "right_panel_visible" in ui_state:
        return bool(ui_state.get("right_panel_visible"))
    return True


def _bottom_panel_visible(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict) and "bottom_panel_visible" in ui_state:
        return bool(ui_state.get("bottom_panel_visible"))
    return False


def _persist_runtime_layout_state(client):
    try:
        shell = client.get_grid_layout_state("app.shell")
        center = client.get_grid_layout_state("app.center")
        shell_snapshot = client.snapshot_widget("app.shell")
        window_state = client.get_window_state()
        columns = shell.get("columns") or []
        center_rows = center.get("rows") or []
        nav_width = None
        ai_width = None
        bottom_height = None
        right_panel_visible = _right_panel_visible()
        bottom_panel_visible = _bottom_panel_visible()
        if len(columns) > 1:
            nav_width = _layout_value(columns[1].get("computed_size"), 220)
        if len(columns) > 3 and right_panel_visible:
            ai_width = _layout_value(columns[3].get("computed_size"), 320)
        if len(center_rows) > 1 and bottom_panel_visible:
            bottom_height = _layout_value(center_rows[1].get("computed_size"), 220)
        bounds = (shell_snapshot or {}).get("bounds") or {}
        updates = {
            "window": {
                "width": _window_value(bounds.get("width"), 1080),
                "height": _window_value(bounds.get("height"), 760, minimum=480),
                "maximized": bool((window_state or {}).get("maximized", False)),
            },
            "layout": {
                "nav_width": nav_width if nav_width is not None else 220,
            },
        }
        if ai_width is not None:
            updates["layout"]["ai_width"] = ai_width
        if bottom_height is not None:
            updates["layout"]["bottom_height"] = bottom_height
        _save_ide_state(updates)
    except Exception:
        pass


def _breadcrumb_for(path: str) -> str:
    home = str(Path.home())
    p = Path(path)
    try:
        rel = p.relative_to(home)
        parts = ["Home"] + list(rel.parts)
    except ValueError:
        parts = list(p.parts)
    return " › ".join(parts) if parts else path


def _open_file_items(path: str) -> list:
    """Return {id, label} dicts for entries (dirs + files) under path."""
    entries = []
    parent = str(Path(path).parent)
    if parent != path:
        entries.append({"id": parent, "label": ".."})
    try:
        names = sorted(os.listdir(path), key=str.lower)
        for name in names:
            if name.startswith("."):
                continue
            full = os.path.join(path, name)
            if os.path.isdir(full):
                entries.append({"id": full, "label": name + "/"})
            else:
                entries.append({"id": full, "label": name})
    except OSError:
        pass
    return entries


def build_open_file_dialog(initial_path: str):
    """Build a high-quality open-file dialog using the current Elara UI API.

    Current UI API limitations are represented as deliberate placeholders rather
    than fake controls:
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
    parser.add_argument("--ai-rpc-port", default=18779, type=int, help="AI logic-side RPC port (0 to disable)")
    args = parser.parse_args()

    client_ref = {}
    wizard_state = {}            # live checkbox state for the new-project wizard
    nav_state = {}               # current browse path in the wizard file picker
    open_project_nav_state = {}  # current browse path in the open-project dialog
    open_file_nav_state = {}     # current browse path in the open-file dialog
    app_state = {}               # persistent project state set after universal creation
    new_file_state = {}          # live state for the new-file dialog
    new_file_nav_state = {}      # current browse path in the new-file dialog
    editor_state = {}
    ingress_editor_state = {}    # live state for the ingress profile editor window
    app_state["active_editor_tab"] = INITIAL_E_TABS[0][0] if INITIAL_E_TABS else ""
    app_state["theme"] = "dark"
    app_state["nav_view"] = "files"
    app_state["debug_vm_started"] = False
    initial_state = _current_layout_state()
    initial_layout = initial_state.get("layout", {}) if isinstance(initial_state, dict) else {}
    app_state["right_panel_visible"] = _right_panel_visible(initial_state)
    app_state["right_panel_width"] = _layout_value(initial_layout.get("ai_width"), 320)
    app_state["bottom_panel_visible"] = _bottom_panel_visible(initial_state)
    app_state["bottom_panel_height"] = _layout_value(initial_layout.get("bottom_height"), 220)
    tab_list = []                # [{"tab_id", "path", "index", "preview"}]
    tab_click_state = {}         # double-click detection: {"path", "time"}
    ai_state = {
        "messages":     [],      # [{"role": "user"|"assistant", "content": str}]
        "model":        "claude-sonnet-4-6",
        "ctx_file":     True,
        "ctx_project":  False,
        "ctx_selection": False,
        "input_text":   "",
        "generating":   False,
        "cancel_event": None,
    }
    terminal_state = {
        "cwd": "",
        "input": "",
        "output": "Terminal ready. Open a project to set the working directory.",
    }

    # AI RPC bindings — callbacks are set later, once the inner closures exist.
    ide_bindings = IdeBindings()
    ide_bindings.tab_list = tab_list
    ide_bindings.editor_state = editor_state
    ide_bindings.app_state = app_state
    ide_bindings.ai_state = ai_state
    ide_bindings._language_for_path = _editor_language_for_path
    ai_rpc_server: AiRpcServer | None = None

    # --- Event log -----------------------------------------------------------
    # Ring buffer of all significant IDE events. Each entry is a plain dict
    # with at minimum {"ts": float, "type": str}. Written to a per-session
    # JSONL file in artifacts/ so it survives process crashes.
    _event_log: list = []
    _event_log_lock = threading.Lock()
    _event_log_max = 2000
    _event_log_fh: list = [None]   # mutable ref so nested closures can reassign

    def _push_event(event_type: str, **details):
        entry = {"ts": time.time(), "type": event_type}
        entry.update(details)
        with _event_log_lock:
            _event_log.append(entry)
            if len(_event_log) > _event_log_max:
                del _event_log[: len(_event_log) - _event_log_max]
        fh = _event_log_fh[0]
        if fh is not None:
            try:
                fh.write(json.dumps(entry, ensure_ascii=False, default=str) + "\n")
                fh.flush()
            except Exception:
                pass

    def _event_log_recent(n: int = 30) -> list:
        with _event_log_lock:
            return list(_event_log[-n:])

    # Trim large string values before storing in event log (keeps log readable).
    def _trim_for_log(params, max_str: int = 300):
        if not isinstance(params, dict):
            return params
        out = {}
        for k, v in params.items():
            if isinstance(v, str) and len(v) > max_str:
                out[k] = f"{v[:max_str]}…[{len(v)} chars]"
            else:
                out[k] = v
        return out

    # --- Exception log -------------------------------------------------------
    _exception_log: list = []
    _exception_log_lock = threading.Lock()
    _exception_log_max = 200

    def _push_exception(exc, context: str = "unknown"):
        import traceback as _tb
        tb_text = ("".join(_tb.format_exception(type(exc), exc, exc.__traceback__))
                   if isinstance(exc, BaseException) else "")
        recent = _event_log_recent(30)
        entry = {
            "ts": time.time(),
            "context": context,
            "type": type(exc).__name__ if isinstance(exc, BaseException) else "str",
            "message": str(exc),
            "traceback": tb_text,
            "recent_events": recent,
        }
        with _exception_log_lock:
            _exception_log.append(entry)
            if len(_exception_log) > _exception_log_max:
                del _exception_log[: len(_exception_log) - _exception_log_max]
        _push_event("exception", context=context,
                    exc_type=entry["type"], message=entry["message"],
                    traceback=tb_text)

    _orig_excepthook = sys.excepthook

    def _excepthook(exc_type, exc_val, exc_tb):
        _push_exception(exc_val, "uncaught")
        _orig_excepthook(exc_type, exc_val, exc_tb)

    sys.excepthook = _excepthook
    threading.excepthook = lambda args: _push_exception(
        args.exc_value if args.exc_value else RuntimeError(repr(args)),
        f"thread:{getattr(args.thread, 'name', '?')}",
    )

    # --- UI server subprocess tracking ---------------------------------------
    _ui_server: dict = {"proc": None, "cmd": [], "output_lines": []}

    def _resolve_ui_server_cmd():
        workspace = Path(__file__).resolve().parent.parent
        candidates = [
            workspace / "libElaraUI" / "build" / "bin" / "elaraui-server",
            workspace / "build" / "bin" / "elaraui-server",
        ]
        default = Path("/usr/local/bin/elaraui-server")
        import os as _os
        binary = str(default)
        for candidate in candidates:
            if candidate.is_file() and _os.access(str(candidate), _os.X_OK):
                binary = str(candidate)
                break
        return [binary, "--port", str(args.port), "--persistent"]

    # --- epa-dbg subprocess + client -----------------------------------------
    _epa_dbg: dict = {"proc": None, "client": None, "output_lines": [], "port": None, "build_output": ""}

    def _epa_dbg_binary():
        workspace = Path(__file__).resolve().parent.parent
        return workspace / "epa-dbg" / "build" / "epa-dbg"

    def _epa_dbg_port() -> int | None:
        port = _epa_dbg.get("port")
        return int(port) if port else None

    def _allocate_epa_dbg_port() -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            return int(s.getsockname()[1])

    def _epa_dbg_client() -> "EpaDbgClient | None":
        return _epa_dbg.get("client")

    def _epa_dbg_running() -> bool:
        proc = _epa_dbg.get("proc")
        return bool(proc and proc.poll() is None and _epa_dbg.get("client"))

    def _epa_dbg_set_vm_status(state: str, detail: str = ""):
        """Update the visible VM status indicator in the Debug panel."""
        ui_c = client_ref.get("client")
        if not ui_c:
            return

        labels = {
            "idle":     ("●  VM idle", "#777777"),
            "starting": ("●  VM starting", "#c7922b"),
            "running":  ("●  VM running", "#3ea35f"),
            "stopping": ("●  VM stopping", "#c7922b"),
            "error":    ("●  VM error", "#c85151"),
        }
        text, color = labels.get(state, (f"●  VM {state}", "#777777"))
        if detail:
            text = f"{text}: {detail}"
        try:
            ui_c.set_text("nav.debug.vm_status", text)
        except Exception:
            pass

    def _epa_dbg_set_vm_button(running: bool):
        """Update the Start/Reset button label to reflect current VM state."""
        ui_c = client_ref.get("client")
        if not ui_c:
            return
        label = "⟳  Reset" if running else "▶  Start"
        try:
            ui_c.set_text("nav.debug.vm_reset", label)
            ui_c.set_enabled("nav.debug.vm_stop", running)
        except Exception:
            pass
        _epa_dbg_set_vm_status("running" if running else "idle")

    def _epa_dbg_log(line: str):
        """Append a line to the epa-dbg output buffer and push to the build output panel."""
        buf = _epa_dbg.get("build_output", "") + line + "\n"
        buf = buf[-32000:]  # cap at ~32k chars
        _epa_dbg["build_output"] = buf
        try:
            ui_c = client_ref.get("client")
            if ui_c:
                ui_c.set_text("bottom.build_output", buf)
                ui_c.set_visible("bottom.panel", True)
                ui_c.set_visible("bottom.build_output", True)
                ui_c.set_visible("bottom.terminal_panel", False)
        except Exception:
            pass

    def _epa_dbg_show_error(title: str, message: str, artifact_lines: list | None = None):
        """Write an error artifact and show an error dialog in the UI."""
        artifacts_dir = Path(__file__).resolve().parent / "artifacts"
        artifacts_dir.mkdir(exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        artifact_path = artifacts_dir / f"epa-dbg-error-{stamp}.txt"
        try:
            artifact_path.write_text(
                f"{title}\n{'='*60}\n{message}\n"
                + ("\n--- process output ---\n" + "\n".join(artifact_lines) if artifact_lines else ""),
                encoding="utf-8",
            )
        except Exception:
            pass
        ui_c = client_ref.get("client")
        _epa_dbg_set_vm_status("error", title.replace("epa-dbg: ", ""))
        if ui_c:
            try:
                ui_c.open_window(
                    "epa-dbg-error",
                    title,
                    520, 220,
                    build_error_dialog(title, f"{message}\n\nDetails written to:\n{artifact_path}"),
                )
            except Exception:
                pass

    def _epa_dbg_launch():
        """Start epa-dbg if not already running, connect client."""
        from epa_dbg_client import EpaDbgClient

        _epa_dbg_set_vm_status("starting")
        proc = _epa_dbg.get("proc")
        if proc and proc.poll() is None:
            if _epa_dbg.get("client") and _epa_dbg["client"].connected:
                _epa_dbg_set_vm_button(True)
                return
            # Process alive but client gone — reconnect.
            port = _epa_dbg_port()
            if not port:
                _epa_dbg_show_error(
                    "epa-dbg: reconnect failed",
                    "epa-dbg is running but no port was recorded for this session.",
                    list(_epa_dbg.get("output_lines", [])),
                )
                return
            try:
                c = EpaDbgClient("127.0.0.1", port)
                c.connect_retry(timeout=5.0)
                _epa_dbg["client"] = c
                _epa_dbg_set_vm_button(True)
            except Exception as exc:
                _epa_dbg_show_error(
                    "epa-dbg: reconnect failed",
                    f"Could not reconnect to epa-dbg on port {port}.\n{exc}",
                    list(_epa_dbg.get("output_lines", [])),
                )
            return

        binary = _epa_dbg_binary()
        if not binary.is_file():
            _epa_dbg_set_vm_button(False)
            _epa_dbg_show_error(
                "epa-dbg: binary not found",
                f"Binary not found at:\n{binary}\n\nRun: make  in the epa-dbg directory.",
            )
            return

        out_lines = _epa_dbg.setdefault("output_lines", [])
        out_lines.clear()
        port = _allocate_epa_dbg_port()
        _epa_dbg["port"] = port

        def _reader(stream, tag):
            for raw in stream:
                line = raw.decode(errors="replace").rstrip()
                out_lines.append(f"[{tag}] {line}")
                if len(out_lines) > 500:
                    del out_lines[: len(out_lines) - 500]
                _epa_dbg_log(f"[{tag}] {line}")

        try:
            new_proc = subprocess.Popen(
                [str(binary), str(port), "127.0.0.1"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except OSError as exc:
            _epa_dbg_set_vm_button(False)
            _epa_dbg_show_error(
                "epa-dbg: failed to start",
                f"Could not execute:\n{binary}\n\n{exc}",
            )
            return

        threading.Thread(target=_reader, args=(new_proc.stdout, "stdout"), daemon=True).start()
        threading.Thread(target=_reader, args=(new_proc.stderr, "stderr"), daemon=True).start()
        _epa_dbg["proc"] = new_proc
        _epa_dbg_log(f"[epa-dbg] started on port {port}")

        # Give process a moment then check it hasn't immediately exited
        time.sleep(0.3)
        exit_code = new_proc.poll()
        if exit_code is not None:
            time.sleep(0.5)   # let readers drain
            _epa_dbg_show_error(
                "epa-dbg: process exited immediately",
                f"epa-dbg exited with code {exit_code} before accepting connections.",
                list(out_lines),
            )
            _epa_dbg["proc"] = None
            _epa_dbg["port"] = None
            return

        try:
            c = EpaDbgClient("127.0.0.1", port)
            c.connect_retry(timeout=8.0)
            _epa_dbg["client"] = c
            _epa_dbg_set_vm_button(True)
        except Exception as exc:
            exit_code = new_proc.poll()
            time.sleep(0.3)
            _epa_dbg_show_error(
                "epa-dbg: connection failed",
                f"Process started on port {port} (exit={exit_code}) but TCP connect failed.\n{exc}",
                list(out_lines),
            )
            _epa_dbg["proc"] = None
            _epa_dbg["port"] = None

    def _epa_dbg_stop():
        """Terminate epa-dbg process and close client."""
        _epa_dbg_set_vm_status("stopping")
        c = _epa_dbg.get("client")
        if c:
            try:
                c.close()
            except Exception:
                pass
            _epa_dbg["client"] = None

        proc = _epa_dbg.get("proc")
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        _epa_dbg["proc"] = None
        _epa_dbg["port"] = None
        _epa_dbg_set_vm_button(False)

    def _epa_dbg_reset(kernel_id: int = 0) -> dict:
        """Ensure epa-dbg is running and reset the kernel slot."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epa-dbg not available"}
        try:
            result = c.reset(kernel_id)
            return {"ok": True, "result": result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _epa_dbg_load_bundle(bundle_path: str, kernel_id: int = 0) -> dict:
        """Load a compiled .epabin bundle into the debug kernel."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epa-dbg not available"}
        try:
            load_result = c.load_bundle(kernel_id, bundle_path)
            return {"ok": True, "load": load_result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _epa_dbg_load_asm(asm_text: str, kernel_id: int = 0) -> dict:
        """Load EPA assembly text directly into the debug kernel."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epa-dbg not available"}
        try:
            reset_result = c.reset(kernel_id)
            load_result = c.load_asm(kernel_id, asm_text)
            return {"ok": True, "reset": reset_result, "load": load_result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _compiler_root():
        return Path(__file__).resolve().parent.parent / "libElaraParallelAssembly" / "e"

    def _compiler_binary():
        return _compiler_root() / ".." / "build" / "e" / "e2epa"

    def _semantic_binary():
        return _compiler_root() / ".." / "build" / "e" / "ec"

    def _bundle_binary():
        return _compiler_root() / ".." / "build" / "e" / "e2epabin"

    def _project_builder_root():
        return Path(__file__).resolve().parent.parent / "ElaraProjectBuilder"

    def _project_builder_binary():
        return _project_builder_root() / "build" / "elara-project-builder"

    def _ensure_project_builder():
        builder = _project_builder_binary()
        if builder.is_file():
            return builder

        subprocess.run(
            ["make", "-C", str(_project_builder_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return builder

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

    def _ensure_e2epabin():
        bundle = _bundle_binary()
        if bundle.is_file():
            return bundle
        subprocess.run(
            ["make", "-C", str(_compiler_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return bundle

    def _project_meta(project_root: Path) -> dict:
        meta_path = project_root / ".elaraproject" / "project.json"
        try:
            return json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def _history_dir() -> Path | None:
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        d = Path(project_root) / ".elaraproject" / "history"
        try:
            d.mkdir(parents=True, exist_ok=True)
        except Exception:
            return None
        return d

    def _save_history(tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        d = _history_dir()
        if not d:
            return
        data = {
            "undo_stack": state.get("undo_stack", [])[-100:],
            "redo_stack": state.get("redo_stack", [])[-100:],
        }
        try:
            (d / f"{tab_id}.json").write_text(json.dumps(data), encoding="utf-8")
        except Exception:
            pass

    def _load_history(tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        d = _history_dir()
        if not d:
            return
        try:
            data = json.loads((d / f"{tab_id}.json").read_text(encoding="utf-8"))
            state["undo_stack"] = data.get("undo_stack", [])
            state["redo_stack"] = data.get("redo_stack", [])
        except Exception:
            pass

    def _project_e_compile_units(project_root: Path) -> list[Path]:
        epa_root = project_root / "epa"
        if not epa_root.is_dir():
            return []
        entry = epa_root / "entry.e"
        if not entry.is_file():
            raise RuntimeError(f"Missing root kernel compile unit: {entry}")
        others = sorted(
            p for p in epa_root.rglob("*.e")
            if p.is_file() and p.name != "entry.e"
        )
        return [entry] + others

    def _run_subprocess(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
        return subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env={**os.environ, "LC_ALL": "C"},
        )

    def _build_project(client, rebuild: bool = False):
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            print(json.dumps({"build": "skipped", "reason": "no_project_open"}, indent=2), flush=True)
            try:
                client.set_text("bottom.build_output", "Build skipped: no project open.")
            except Exception:
                pass
            return

        project_root = Path(project_root_text)
        meta = _project_meta(project_root)
        technologies = set(meta.get("technologies", []))
        build_steps = []
        build_log = [f"Build started: {project_root}"]

        try:
            if "epa" in technologies:
                bundle = _ensure_e2epabin()
                build_dir = project_root / "build"
                build_dir.mkdir(parents=True, exist_ok=True)
                units = _project_e_compile_units(project_root)
                cmd = [str(bundle), "--out", str(build_dir / "epa.bin")] + [str(p) for p in units]
                build_log.append("$ " + " ".join(cmd))
                result = _run_subprocess(cmd, cwd=project_root)
                if result.stdout.strip():
                    build_log.append(result.stdout.strip())
                if result.stderr.strip():
                    build_log.append(result.stderr.strip())
                build_steps.append({
                    "step": "epa_bundle",
                    "command": cmd,
                    "stdout": result.stdout.strip(),
                    "stderr": result.stderr.strip(),
                    "outputs": {
                        "bundle": str(build_dir / "epa.bin"),
                        "epaasm_dir": str(build_dir / "epaasm"),
                        "blobs_dir": str(build_dir / "blobs"),
                    },
                })

            if "cpp" in technologies:
                cpp_root = project_root / "cpp"
                build_script = cpp_root / "build.sh"
                if not build_script.is_file():
                    raise RuntimeError(f"Missing C++ build script: {build_script}")
                if rebuild:
                    clean_script = cpp_root / "clean.sh"
                    if clean_script.is_file():
                        build_log.append("$ " + str(clean_script))
                        clean_result = _run_subprocess([str(clean_script)], cwd=cpp_root)
                        if clean_result.stdout.strip():
                            build_log.append(clean_result.stdout.strip())
                        if clean_result.stderr.strip():
                            build_log.append(clean_result.stderr.strip())
                        build_steps.append({
                            "step": "cpp_clean",
                            "command": [str(clean_script)],
                            "stdout": clean_result.stdout.strip(),
                            "stderr": clean_result.stderr.strip(),
                        })
                build_log.append("$ " + str(build_script))
                cpp_result = _run_subprocess([str(build_script)], cwd=cpp_root)
                if cpp_result.stdout.strip():
                    build_log.append(cpp_result.stdout.strip())
                if cpp_result.stderr.strip():
                    build_log.append(cpp_result.stderr.strip())
                build_steps.append({
                    "step": "cpp_build",
                    "command": [str(build_script)],
                    "stdout": cpp_result.stdout.strip(),
                    "stderr": cpp_result.stderr.strip(),
                })

            print(json.dumps({
                "build": "ok",
                "project": str(project_root),
                "steps": build_steps,
            }, indent=2), flush=True)
            build_log.append("Build complete.")
            try:
                client.set_text("bottom.build_output", "\n\n".join(build_log))
            except Exception:
                pass
            _open_project(client, project_root)
        except (subprocess.CalledProcessError, RuntimeError) as exc:
            message = ""
            command = None
            if isinstance(exc, subprocess.CalledProcessError):
                message = (exc.stderr or exc.stdout or str(exc)).strip()
                command = exc.cmd
            else:
                message = str(exc)
            print(json.dumps({
                "build": "failed",
                "project": str(project_root),
                "command": command,
                "message": message,
            }, indent=2), flush=True)
            build_log.append("Build failed.")
            build_log.append(message)
            try:
                client.set_text("bottom.build_output", "\n\n".join(build_log))
            except Exception:
                pass

    def _clean_project():
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            print(json.dumps({"clean": "skipped", "reason": "no_project_open"}, indent=2), flush=True)
            return
        project_root = Path(project_root_text)
        removed = []
        build_dir = project_root / "build"
        if build_dir.exists():
            shutil.rmtree(build_dir)
            removed.append(str(build_dir))
        cpp_root = project_root / "cpp"
        clean_script = cpp_root / "clean.sh"
        if clean_script.is_file():
            try:
                result = _run_subprocess([str(clean_script)], cwd=cpp_root)
                print(json.dumps({
                    "clean": "ok",
                    "project": str(project_root),
                    "removed": removed,
                    "cpp_clean": {
                        "stdout": result.stdout.strip(),
                        "stderr": result.stderr.strip(),
                    },
                }, indent=2), flush=True)
                return
            except subprocess.CalledProcessError as exc:
                print(json.dumps({
                    "clean": "failed",
                    "project": str(project_root),
                    "command": exc.cmd,
                    "message": (exc.stderr or exc.stdout or str(exc)).strip(),
                }, indent=2), flush=True)
                return
        print(json.dumps({
            "clean": "ok",
            "project": str(project_root),
            "removed": removed,
        }, indent=2), flush=True)

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

    def _build_trace_nodes(values: list, root_id: str):
        root = {"id": root_id, "label": "Stack (LIFO)", "expanded": True, "children": []}
        if not values:
            root["children"].append({"id": f"{root_id}.empty", "label": "Stack empty"})
        else:
            for i, v in enumerate(values):
                label = f"0x{v & 0xFFFFFFFF:08X}" + (" ← TOS" if i == 0 else "")
                root["children"].append({"id": f"{root_id}.{i}", "label": label})
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

    def _debug_preview_text(epa_text: str, marker_line: int | None = None, radius: int = 5,
                            epa_map: list = None):
        if not epa_text.strip():
            return "# Debug Trace\n\nNo EPA output available.\n"
        lines = epa_text.splitlines()
        marker = _first_marker_line(epa_text) if marker_line is None else max(0, min(marker_line, len(lines) - 1))
        e_source_line = 0
        if epa_map and 0 <= marker < len(epa_map):
            e_source_line = epa_map[marker]
        start = max(0, marker - radius)
        end = min(len(lines), marker + radius + 1)
        header = [f"# Debug Trace", "", f"epa_line={marker + 1}"]
        if e_source_line > 0:
            header.append(f"e_source_line={e_source_line}")
        header.append("")
        width = len(str(end))
        body = []
        for idx in range(start, end):
            prefix = ">>" if idx == marker else "  "
            body.append(f"{prefix} {idx + 1:>{width}} | {lines[idx]}")
        return "\n".join(header + body) + "\n"

    def _analyze_e_source(source_text: str, ids: dict, source_dir: Path = None):
        semantic = _ensure_ec()
        with tempfile.TemporaryDirectory(prefix="epa-ide-ec-") as tmp:
            tmp_path = Path(tmp)
            if source_dir and source_dir.is_dir():
                source_path = source_dir / f"._epa_ide_buf_{os.getpid()}.e"
            else:
                source_path = tmp_path / "buffer.e"
            source_path.write_text(source_text, encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(semantic), str(source_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            finally:
                if source_dir and source_path.exists():
                    source_path.unlink(missing_ok=True)
            if proc.returncode != 0:
                message = (proc.stderr or proc.stdout or "semantic analysis failed").strip()
                return {
                    "ok": False,
                    "message": message,
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                    "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
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
            dynamic_block = _extract_section_block(stdout, "dynamic-memory")
            return {
                "ok": True,
                "message": "",
                "ghs_nodes": _parse_tree_lines(ghs_block or "No declared GHS layouts.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("\n".join(stack_lines).strip() or "No stack frame data.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("\n".join(local_lines).strip() or "No local arena allocations.", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines(dynamic_block or "No dynamic allocations.", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
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

    def _compile_e_source(source_text: str, source_dir: Path = None):
        compiler = _ensure_e2epa()
        with tempfile.TemporaryDirectory(prefix="epa-ide-e2epa-") as tmp:
            tmp_path = Path(tmp)
            if source_dir and source_dir.is_dir():
                source_path = source_dir / f"._epa_ide_buf_{os.getpid()}.e"
            else:
                source_path = tmp_path / "buffer.e"
            output_path = tmp_path / "buffer.epaasm"
            map_path = tmp_path / "buffer.epamap"
            source_path.write_text(source_text, encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(compiler), str(source_path), str(output_path), str(map_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            finally:
                if source_dir and source_path.exists():
                    source_path.unlink(missing_ok=True)
            if proc.returncode == 0:
                epa_text = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
                epa_block_map: dict = {}  # {(block_type, block_id): [(byte_offset, src_line), ...]}
                if map_path.exists():
                    cur_block = None
                    for raw in map_path.read_text(encoding="utf-8").splitlines():
                        raw = raw.strip()
                        if not raw:
                            continue
                        if raw.startswith("B "):
                            parts = raw.split()
                            if len(parts) == 3:
                                try:
                                    cur_block = (int(parts[1]), int(parts[2]))
                                    epa_block_map[cur_block] = []
                                except ValueError:
                                    cur_block = None
                        elif cur_block is not None:
                            parts = raw.split()
                            if len(parts) == 2:
                                try:
                                    epa_block_map[cur_block].append((int(parts[0]), int(parts[1])))
                                except ValueError:
                                    pass
                return {"ok": True, "epa_text": epa_text, "epa_block_map": epa_block_map, "diagnostics": [], "message": ""}
            message = (proc.stderr or proc.stdout or "compile failed").strip()
            return {
                "ok": False,
                "epa_text": "",
                "epa_block_map": {},
                "diagnostics": _diagnostic_from_error(message),
                "message": message,
            }

    def _apply_editor_view(client, tab_id: str, set_focus: bool = False):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        view = state.get("view", "e")
        is_epa = view == "epa"
        client.set_visible(ids["source"], not is_epa)
        client.set_visible(ids["epa"], is_epa)
        client.set_enabled(ids["button_e"], view != "e")
        client.set_enabled(ids["button_epa"], view != "epa")
        client.set_read_only(ids["epa"], True)
        debug_on = state.get("debug", False)
        try:
            client.set_grid_column_exact_size(ids["debug_panel"], 1, 320 if debug_on else 0)
        except Exception:
            pass
        if set_focus:
            _focus_editor_widget(client, tab_id, state)

    def _refresh_debug_controls(client, tab_id: str):
        pass  # source/epa editors are the same widgets, no separate debug view to sync

    def _refresh_debug_sidebars(client, tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        _replace_tree_nodes(client, ids["debug"], state.get("trace_nodes", _build_trace_nodes([], f"{ids['debug']}.root")))
        _replace_tree_nodes(client, ids["debug_ghs"], state.get("ghs_nodes", _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root")))
        _replace_tree_nodes(client, ids["debug_stack"], state.get("stack_nodes", _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root")))
        _replace_tree_nodes(client, ids["debug_local"], state.get("local_nodes", _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root")))
        _replace_tree_nodes(client, ids["debug_dynamic"], state.get("dynamic_nodes", _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root")))

    _WORKER_DEF_RE = re.compile(
        r"^\s*worker\s+([A-Za-z_]\w*)\s*\(\s*"  # capture worker name
        r"([A-Za-z_]\w*(?:\|[A-Za-z_]\w*)*)"    # capture type(s), possibly union A|B
        r"\s+[A-Za-z_]",                          # followed by the variable name
        re.MULTILINE,
    )

    def _workers_in_file(path: Path) -> list:
        """Return list of {"name": ..., "types": [...]} for each worker in a file."""
        try:
            src = path.read_text(encoding="utf-8", errors="replace")
        except Exception:
            return []
        result = []
        for m in _WORKER_DEF_RE.finditer(src):
            name = m.group(1)
            types = [t.strip() for t in m.group(2).split("|") if t.strip()]
            result.append({"name": name, "types": types})
        return result

    def _kernels_from_project() -> list:
        """Return list of {"id": kernel_id, "label": kernel_label, "path": str} from epa/*.e files."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        if not epa_root or not epa_root.is_dir():
            return []
        entry = epa_root / "entry.e"
        others = sorted(p for p in epa_root.rglob("*.e") if p.is_file() and p != entry)
        paths = ([entry] if entry.is_file() else []) + others
        result = []
        for path in paths:
            rel = path.relative_to(epa_root)
            kernel_id = ".".join(rel.with_suffix("").parts)
            kernel_label = str(rel.with_suffix(""))
            result.append({"id": kernel_id, "label": kernel_label, "path": str(path)})
        return result

    def _ingress_types_from_project(kernel_id: str = "", worker_name: str = "") -> list:
        """Scan epa/**/*.e and *.em files for worker parameter types, optionally filtered."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        seen: set = set()
        type_names: list = []

        def _collect_src(src: str, name_filter: str = ""):
            for m in _WORKER_DEF_RE.finditer(src):
                if name_filter and m.group(1) != name_filter:
                    continue
                for t in m.group(2).split("|"):
                    t = t.strip()
                    if t and t not in seen:
                        seen.add(t)
                        type_names.append(t)

        if kernel_id and epa_root:
            rel = kernel_id.replace(".", "/") + ".e"
            kpath = epa_root / rel
            if kpath.is_file():
                _collect_src(kpath.read_text(encoding="utf-8", errors="replace"), worker_name)
        elif epa_root and epa_root.is_dir():
            for path in sorted(epa_root.rglob("*")):
                if not path.is_file() or path.suffix not in (".e", ".em"):
                    continue
                try:
                    _collect_src(path.read_text(encoding="utf-8", errors="replace"))
                except Exception:
                    pass
            for state in editor_state.values():
                _collect_src(state.get("source_text", ""))

        return [{"id": n, "label": n} for n in type_names]

    def _apply_combo_items(client, target: str, items: list):
        try:
            client.call("ui.setSectionJson", {
                "target": target,
                "section": "items",
                "value": items,
            })
        except Exception:
            pass

    def _apply_ingress_types_combo(client, items):
        _apply_combo_items(client, "nav.debug.ingress_type", items)
        cur = app_state.get("debug_ingress_type", "")
        valid_ids = [it["id"] for it in items]
        if items and (not cur or cur not in valid_ids):
            first_type = items[0]["id"]
            app_state["debug_ingress_type"] = first_type
            _refresh_ingress_profiles_list(client, first_type)

    def _refresh_debug_panel(client):
        """Rebuild nav.debug.kernels, kernel combo, and ingress type combo."""
        kernels = _kernels_from_project()

        # Kernel list rows
        children = []
        if kernels:
            list_ui = UiDocumentBuilder()
            for k in kernels:
                _build_kernel_row_widgets(list_ui, k["id"], k["label"])
            for k in kernels:
                child_dict = list_ui.widget_dict(f"nav.debug.kernel.{k['id']}")
                child_dict["entry"] = {"height": 52}
                children.append(child_dict)

        try:
            client.call("ui.replaceChildren", {
                "target": "nav.debug.kernels",
                "document": json.dumps({"children": children}, separators=(",", ":"), ensure_ascii=False),
            })
        except Exception:
            pass

        # Populate per-kernel worker combos
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        for k in kernels:
            if not epa_root:
                break
            kpath = epa_root / (k["id"].replace(".", "/") + ".e")
            workers = _workers_in_file(kpath)
            items = [{"id": w["name"], "label": w["name"]} for w in workers]
            app_state[f"debug_kernel_workers_{k['id']}"] = workers
            try:
                _apply_combo_items(client, f"nav.debug.kernel.{k['id']}.worker", items)
                _apply_cached_kernel_indicator_state(client, k["id"])
                _apply_cached_kernel_queue_state(client, k["id"])
            except Exception:
                pass

        # Kernel combo
        kernel_items = [{"id": k["id"], "label": k["label"]} for k in kernels]
        _apply_combo_items(client, "nav.debug.ingress_kernel", kernel_items)

        # Worker combo: use currently selected kernel, default to first kernel
        sel_kernel = app_state.get("debug_ingress_kernel", "")
        if not sel_kernel and kernels:
            sel_kernel = kernels[0]["id"]
            app_state["debug_ingress_kernel"] = sel_kernel
        _refresh_ingress_worker_combo(client, sel_kernel)

        # Type combo: serve cache immediately, refresh in background
        cached = app_state.get("debug_ingress_types_cache")
        if cached is not None:
            _apply_ingress_types_combo(client, cached)

        def _bg_refresh_types():
            sel_k = app_state.get("debug_ingress_kernel", "")
            sel_w = app_state.get("debug_ingress_worker", "")
            items = _ingress_types_from_project(sel_k, sel_w)
            app_state["debug_ingress_types_cache"] = items
            c = client_ref.get("client")
            if c:
                _apply_ingress_types_combo(c, items)

        import threading
        threading.Thread(target=_bg_refresh_types, daemon=True).start()

    def _refresh_ingress_worker_combo(client, kernel_id: str):
        """Populate the worker combo for the given kernel_id."""
        workers = []
        if kernel_id:
            project_root = app_state.get("project_root", "")
            epa_root = Path(project_root) / "epa" if project_root else None
            if epa_root:
                rel = kernel_id.replace(".", "/") + ".e"
                kpath = epa_root / rel
                workers = _workers_in_file(kpath)
        items = [{"id": w["name"], "label": w["name"]} for w in workers]
        _apply_combo_items(client, "nav.debug.ingress_worker", items)

    _TYPE_DEF_RE = re.compile(
        r"\btype\s+([A-Za-z_]\w*)\s*\(([^)]*)\)",
        re.DOTALL,
    )
    _FIELD_RE = re.compile(r"\b([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*(?:,|$)")

    def _parse_type_defs() -> dict:
        """Scan all .em files in epa/ and return {TypeName: [field_name, ...]}."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        result: dict = {}
        if not epa_root or not epa_root.is_dir():
            return result
        for path in sorted(epa_root.rglob("*.em")):
            try:
                src = path.read_text(encoding="utf-8", errors="replace")
            except Exception:
                continue
            for m in _TYPE_DEF_RE.finditer(src):
                type_name = m.group(1)
                params_str = m.group(2)
                fields = []
                for fm in _FIELD_RE.finditer(params_str):
                    fields.append(fm.group(2))
                if type_name not in result:
                    result[type_name] = fields
        return result

    def _profiles_dir(type_name: str) -> Path | None:
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        return Path(project_root) / "epa" / "profiles" / type_name

    def _profiles_for_type(type_name: str) -> list:
        """Return list of {"id": name, "label": name} for saved profiles of type_name."""
        d = _profiles_dir(type_name)
        if not d or not d.is_dir():
            return []
        items = []
        for p in sorted(d.glob("*.json")):
            name = p.stem
            items.append({"id": name, "label": name})
        return items

    def _save_ingress_profile(type_name: str, profile_name: str, field_values: dict):
        d = _profiles_dir(type_name)
        if d is None:
            return
        d.mkdir(parents=True, exist_ok=True)
        out = {"type": type_name, "name": profile_name, "fields": field_values}
        (d / f"{profile_name}.json").write_text(
            json.dumps(out, indent=2), encoding="utf-8"
        )

    _IND_OFF = "off"
    _IND_GREEN = "green"
    _IND_RED = "red"

    _IND_COLORS = {
        _IND_OFF: "#666666",
        _IND_GREEN: "#2da44e",
        _IND_RED: "#d73a49",
    }

    def _set_kernel_dot(client, kernel_tab_id: str, dot_name: str, state: str):
        if not client:
            return
        color = _IND_COLORS.get(state, _IND_COLORS[_IND_OFF])
        try:
            client.call(
                "ui.setForegroundColor",
                {
                    "target": f"nav.debug.kernel.{kernel_tab_id}.{dot_name}",
                    "color": color,
                },
            )
        except Exception:
            pass

    def _set_kernel_load_indicator(client, kernel_tab_id: str, symbol: str):
        state = app_state.setdefault("debug_kernel_indicator_state", {}).setdefault(kernel_tab_id, {})
        state["load"] = symbol
        if client:
            _set_kernel_dot(client, kernel_tab_id, "load_ind", symbol)

    def _set_kernel_run_indicator(client, kernel_tab_id: str, symbol: str):
        state = app_state.setdefault("debug_kernel_indicator_state", {}).setdefault(kernel_tab_id, {})
        state["run"] = symbol
        if client:
            _set_kernel_dot(client, kernel_tab_id, "run_ind", symbol)

    def _apply_cached_kernel_indicator_state(client, kernel_tab_id: str):
        if not client or not kernel_tab_id:
            return
        state = app_state.get("debug_kernel_indicator_state", {}).get(kernel_tab_id, {})
        _set_kernel_dot(client, kernel_tab_id, "load_ind", state.get("load", _IND_OFF))
        _set_kernel_dot(client, kernel_tab_id, "run_ind", state.get("run", _IND_OFF))

    def _kernel_tab_id_from_bundle(bundle_path: str) -> str:
        if not bundle_path:
            return ""
        try:
            p = Path(bundle_path)
            epa_build = Path(app_state.get("project_root", "")) / "build" / "epa"
            rel = p.relative_to(epa_build).with_suffix("")
            return ".".join(rel.parts)
        except Exception:
            return ""

    def _bundle_path_for_kernel_id(kernel_id: str) -> str:
        project_root = app_state.get("project_root", "")
        if not project_root or not kernel_id:
            return ""
        root = Path(project_root)
        candidates = [
            root / "build" / "epa" / (kernel_id.replace(".", "/") + ".epabin"),
            root / "build" / "epa" / (kernel_id.replace(".", "/") + ".epa.bin"),
            root / "build" / "epa.bin",
        ]
        for candidate in candidates:
            if candidate.is_file():
                return str(candidate)
        return str(candidates[0])

    def _selected_worker_wid_for_kernel(kernel_tab_id: str) -> int | None:
        workers_list = app_state.get(f"debug_kernel_workers_{kernel_tab_id}", [])
        if not workers_list:
            return None
        sel_worker_name = app_state.get(f"debug_kernel_worker_{kernel_tab_id}", "")
        for wi, w in enumerate(workers_list):
            if w.get("name") == sel_worker_name:
                return wi + 1
        return 1

    def _set_kernel_queue_badge(client, kernel_tab_id: str, total_inq: int, sel_inq: int):
        try:
            client.set_text(f"nav.debug.kernel.{kernel_tab_id}.queue", f"{total_inq} / {sel_inq}")
        except Exception:
            pass

    def _clear_kernel_queue_state(client, kernel_tab_id: str):
        if not kernel_tab_id:
            return
        queue_state = app_state.setdefault("debug_kernel_queue_state", {})
        queue_state[kernel_tab_id] = {"total_inq": 0, "worker_inq": {}}
        if client:
            _set_kernel_queue_badge(client, kernel_tab_id, 0, 0)

    def _apply_cached_kernel_queue_state(client, kernel_tab_id: str):
        if not client or not kernel_tab_id:
            return
        queue_state = app_state.get("debug_kernel_queue_state", {}).get(kernel_tab_id, {})
        total_inq = int(queue_state.get("total_inq", 0) or 0)
        worker_inq = queue_state.get("worker_inq", {}) or {}
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        sel_inq = int(worker_inq.get(sel_wid, 0) or 0) if sel_wid is not None else 0
        _set_kernel_queue_badge(client, kernel_tab_id, total_inq, sel_inq)

    def _update_kernel_queue_state_from_snapshot(client, kernel_tab_id: str, snapshot: dict):
        if not kernel_tab_id:
            return
        app_state.setdefault("debug_kernel_snapshot_state", {})[kernel_tab_id] = snapshot
        workers = snapshot.get("workers", [])
        total_inq = sum(w.get("inq_count", 0) for w in workers)
        worker_inq = {}
        for w in workers:
            worker_inq[w.get("wid")] = w.get("inq_count", 0)
        app_state.setdefault("debug_kernel_queue_state", {})[kernel_tab_id] = {
            "total_inq": total_inq,
            "worker_inq": worker_inq,
        }
        if client:
            sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
            sel_inq = worker_inq.get(sel_wid, 0) if sel_wid is not None else 0
            _set_kernel_queue_badge(client, kernel_tab_id, total_inq, sel_inq)

    def _update_kernel_indicator_from_snapshot(client, kernel_tab_id: str, snapshot: dict):
        """Set indicator colour based on the selected worker's state in the snapshot."""
        if not kernel_tab_id:
            return
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)

        color = _IND_OFF
        if sel_wid is not None:
            for w in snapshot.get("workers", []):
                if w.get("wid") == sel_wid:
                    if w.get("faulted"):
                        color = _IND_RED
                    elif w.get("halted"):
                        color = _IND_OFF
                    elif w.get("waiting_for_data"):
                        color = _IND_GREEN
                    else:
                        color = _IND_RED   # still running
                    break
        _set_kernel_run_indicator(client, kernel_tab_id, color)

    def _update_queue_counters(client, snapshot: dict):
        """Update kernel row queue badge with 'total / selected-worker' inq counts."""
        kernel_tab_id = _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))
        if not kernel_tab_id:
            return
        _update_kernel_queue_state_from_snapshot(client, kernel_tab_id, snapshot)
        _update_eip_marker(client, kernel_tab_id, snapshot)

    def _editor_tab_id_for_kernel(kernel_id: str) -> str:
        project_root = app_state.get("project_root", "")
        if not project_root or not kernel_id:
            return ""
        file_path = str(Path(project_root) / "epa" / (kernel_id.replace(".", "/") + ".e"))
        return _tab_id_for_path(file_path)

    _KERNEL_DEF_RE = re.compile(r"^\s*kernel\s*\(", re.MULTILINE)

    def _block_decl_lines(source_text: str) -> dict:
        """Return {(0, block_id): 1-based-line} for kernel entry and each worker in source_text.

        block_id 0 = kernel(...), 1 = first worker, 2 = second worker, etc.
        Entries are used as fallback when the block map has no source line for a given offset.
        """
        lines = source_text.splitlines()
        result = {}
        worker_idx = 1
        for lineno, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("kernel") and re.match(r"kernel\s*\(", stripped):
                result[(0, 0)] = lineno
            elif stripped.startswith("worker") and re.match(r"worker\s+[A-Za-z_]", stripped):
                result[(0, worker_idx)] = lineno
                worker_idx += 1
        return result

    def _eip_to_source_line(epa_block_map: dict, block_type: int, block_id: int, rel_pc: int,
                             block_decl_lines: dict | None = None) -> int:
        """Return 1-based source line for the given EIP, or 0 if not found.

        When the map has no annotation for a prologue instruction (src_line == 0),
        falls back to the declaration line of the entry/worker block.
        """
        entries = epa_block_map.get((int(block_type), int(block_id)), [])
        result = 0
        for (offset, src_line) in entries:
            if offset <= rel_pc:
                result = src_line
            else:
                break
        if result == 0 and block_decl_lines:
            result = block_decl_lines.get((int(block_type), int(block_id)), 0)
        return result

    def _update_eip_marker(client, kernel_tab_id: str, snapshot: dict):
        """Move the EIP marker in the source editor to the selected worker's current line."""
        if not client or not kernel_tab_id:
            return
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if sel_wid is None:
            return
        for w in snapshot.get("workers", []):
            if w.get("wid") != sel_wid:
                continue
            eip = w.get("eip", {})
            block_type = eip.get("block_type", 0)
            block_id = eip.get("block_id", 0)
            rel_pc = eip.get("rel_pc", 0)
            stid = _editor_tab_id_for_kernel(kernel_tab_id)
            st = editor_state.get(stid)
            if not st:
                return
            epa_block_map = st.get("epa_block_map", {})
            source_text = st.get("source_text", "")
            decl_lines = _block_decl_lines(source_text) if source_text else {}
            src_line = _eip_to_source_line(epa_block_map, block_type, block_id, rel_pc, decl_lines)
            try:
                client.set_eip_line(_editor_ids(stid)["source"], max(0, src_line - 1))
            except Exception:
                pass
            return

    def _reset_all_kernel_debug_state(client):
        for kernel in _kernels_from_project():
            kernel_id = kernel["id"]
            _set_kernel_load_indicator(client, kernel_id, _IND_OFF)
            _set_kernel_run_indicator(client, kernel_id, _IND_OFF)
            _clear_kernel_queue_state(client, kernel_id)
        app_state["debug_kernel_snapshot_state"] = {}

    def _start_debug_vm(client, force_restart: bool = False) -> bool:
        if force_restart and _epa_dbg_running():
            _epa_dbg_stop()
        _epa_dbg_launch()
        dbg_c = _epa_dbg_client()
        if not dbg_c:
            return False

        kernels = _kernels_from_project()
        failures = []
        last_loaded = ""
        if client:
            _reset_all_kernel_debug_state(client)

        bundle_targets: dict[str, list[str]] = {}
        for kernel in kernels:
            kernel_id = kernel["id"]
            bundle_path = _bundle_path_for_kernel_id(kernel_id)
            if not bundle_path or not Path(bundle_path).is_file():
                failures.append(kernel_id)
                _set_kernel_load_indicator(client, kernel_id, _IND_RED)
                continue
            bundle_targets.setdefault(bundle_path, []).append(kernel_id)

        for bundle_path, kernel_ids in bundle_targets.items():
            result = _epa_dbg_load_bundle(bundle_path, kernel_id=0)
            if result.get("ok"):
                for kernel_id in kernel_ids:
                    _set_kernel_load_indicator(client, kernel_id, _IND_GREEN)
                last_loaded = bundle_path
            else:
                for kernel_id in kernel_ids:
                    failures.append(kernel_id)
                    _set_kernel_load_indicator(client, kernel_id, _IND_RED)

        if last_loaded:
            app_state["debug_kernel_loaded"] = last_loaded
        else:
            app_state.pop("debug_kernel_loaded", None)
        app_state["debug_vm_started"] = True
        _epa_dbg_set_vm_button(True)

        if failures:
            detail = f"{len(failures)} kernel load failure(s)"
            state = "error" if len(failures) == len(kernels) and kernels else "running"
            _epa_dbg_set_vm_status(state, detail)
        else:
            _epa_dbg_set_vm_status("running", "ready")
        return True

    def _refresh_ingress_profiles_list(client, type_name: str):
        items = _profiles_for_type(type_name) if type_name else []
        app_state["debug_ingress_profiles_cache"] = items
        cur = app_state.get("debug_ingress_selected_profile", "")
        valid_ids = {it["id"] for it in items}
        if items and (not cur or cur not in valid_ids):
            app_state["debug_ingress_selected_profile"] = items[0]["id"]
        elif not items:
            app_state["debug_ingress_selected_profile"] = ""
        try:
            client.replace_list_items("nav.debug.ingress_profiles", items)
        except Exception:
            pass

    def _open_ingress_profile_editor(client, type_name: str):
        try:
            type_defs = _parse_type_defs()
            fields = type_defs.get(type_name, [])
            ingress_editor_state.clear()
            ingress_editor_state["type_name"] = type_name
            ingress_editor_state["fields"] = fields
            ingress_editor_state["field_values"] = {f: "0" for f in fields}
            ingress_editor_state["profile_name"] = ""
            ingress_editor_state["selected_field"] = fields[0] if fields else ""
            doc = build_ingress_profile_editor(type_name, fields)
            client.open_window("ingress-profile-editor", f"New {type_name} Profile", 640, 520, doc)
        except Exception as exc:
            print(json.dumps({"ingress_profile_editor_error": str(exc)}), flush=True)

    def _refresh_e_tab(client, tab_id: str, expected_seq: int | None = None, focus: bool = False):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        source_text = state.get("source_text", "")
        tab_entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
        source_dir = Path(tab_entry["path"]).parent if tab_entry and tab_entry.get("path") else None
        try:
            result = _compile_e_source(source_text, source_dir)
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
        state["epa_block_map"] = result.get("epa_block_map", {})
        state["compile_error"] = result["message"]
        state["available_types"], state["available_workers"] = _extract_debug_candidates(source_text)
        state["trace_nodes"] = _build_trace_nodes([], f"{ids['debug']}.root")
        if result["ok"]:
            try:
                semantic = _analyze_e_source(source_text, ids, source_dir)
            except Exception as exc:
                semantic = {
                    "ok": False,
                    "message": str(exc),
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                    "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
                }
        else:
            semantic = {
                "ok": False,
                "message": result["message"],
                "ghs_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
            }
        state["ghs_nodes"] = semantic["ghs_nodes"]
        state["stack_nodes"] = semantic["stack_nodes"]
        state["local_nodes"] = semantic["local_nodes"]
        state["dynamic_nodes"] = semantic["dynamic_nodes"]
        epa_text_out = result["epa_text"] if result["ok"] else ""
        client.set_text(ids["epa"], epa_text_out)
        client.set_code_editor_diagnostics(ids["source"], result["diagnostics"])
        _apply_editor_view(client, tab_id, set_focus=focus)
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
                "epa_block_map": {},
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "trace_nodes": None,
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
                "available_types": [],
                "available_workers": [],
                "undo_stack": [],
                "redo_stack": [],
                "last_undo_time": 0.0,
                "in_undo_redo": False,
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

    def _open_file_navigate(client, path: str):
        """Navigate the open-file dialog to path and refresh the list."""
        open_file_nav_state["path"] = path
        items = _open_file_items(path)
        try:
            client.replace_list_items("dialog.files", items)
            client.set_text("dialog.location", path)
            client.set_text("dialog.breadcrumb", _breadcrumb_for(path))
            client.set_text("dialog.file_status", f"{len(items)} items")
        except Exception:
            pass

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
        _save_ide_state({"last_project": str(project_path)})
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
        app_state["nav_tree_nodes"] = nodes
        document = json.dumps({"nodes": nodes}, separators=(",", ":"))
        try:
            client.call("ui.replaceChildren", {"target": "nav.tree", "document": document})
        except Exception:
            pass
        try:
            client.set_window_title(f"EPA-IDE : {project_name}")
        except Exception:
            pass
        try:
            client.configure_menu_bar_chrome(
                "app.menu",
                custom_chrome=not _use_system_window_header(),
                window_title=f"EPA-IDE : {project_name}",
            )
        except Exception:
            pass

        app_state.update({
            "project_root": str(project_path),
            "project_name": project_name,
        })
        terminal_state["cwd"] = str(project_path)
        terminal_state["output"] = f"Terminal ready.\nCWD: {terminal_state['cwd']}\n$ "
        try:
            client.call("ui.setVisible", {"target": "nav.no_project", "visible": False})
            client.call("ui.setVisible", {"target": "nav.tree", "visible": True})
            client.call("ui.setVisible", {"target": "app.toolbar", "visible": True})
            client.set_text("bottom.terminal_output", terminal_state["output"])
            _set_project_toolbar_enabled(client, True)
        except Exception:
            pass
        app_state.pop("debug_kernel_loaded", None)
        app_state["debug_vm_started"] = False
        app_state["debug_kernel_indicator_state"] = {}
        app_state["debug_kernel_queue_state"] = {}
        app_state["debug_kernel_snapshot_state"] = {}
        _epa_dbg_set_vm_button(False)
        _close_open_project_window(client)

    _NAV_PANELS = {
        "files":  "nav.panel",
        "search": "nav.search_panel",
        "repo":   "nav.repo_panel",
        "issues": "nav.issues_panel",
        "debug":  "nav.debug_panel",
    }

    def _switch_nav_view(client, view: str):
        if view not in _NAV_PANELS:
            return
        app_state["nav_view"] = view
        for v, panel in _NAV_PANELS.items():
            try:
                client.call("ui.setVisible", {"target": panel, "visible": v == view})
            except Exception:
                pass
        if view == "repo":
            _refresh_repo_panel(client)
            _refresh_ev_panel(client)
        elif view == "issues":
            _refresh_issues_panel(client)
        elif view == "debug":
            _refresh_debug_panel(client)

    def _apply_right_panel_visibility(client, visible: bool):
        width = _layout_value(app_state.get("right_panel_width"), 320)
        try:
            shell = client.get_grid_layout_state("app.shell")
            columns = shell.get("columns") or []
            if len(columns) > 3:
                current_width = columns[3].get("computed_size", 0)
                if visible:
                    saved_width = _layout_value(app_state.get("right_panel_width"), 320)
                    width = saved_width
                elif current_width and current_width >= 120:
                    width = _layout_value(current_width, 320)
                    app_state["right_panel_width"] = width
                    _save_ide_state({"layout": {"ai_width": width}})
        except Exception:
            pass

        try:
            client.set_grid_column_exact_size("app.shell", 3, width if visible else 0)
        except Exception:
            try:
                client.call(
                    "ui.setGridColumnExactSize",
                    {"target": "app.shell", "index": 3, "size": width if visible else 0},
                )
            except Exception:
                pass

        try:
            client.set_visible("ai.panel", visible)
        except Exception:
            pass

    def _toggle_right_panel(client):
        visible = not bool(app_state.get("right_panel_visible", True))
        app_state["right_panel_visible"] = visible
        _save_ide_state({"ui": {"right_panel_visible": visible}})
        _apply_right_panel_visibility(client, visible)

    def _apply_bottom_panel_visibility(client, visible: bool):
        height = _layout_value(app_state.get("bottom_panel_height"), 220)
        try:
            center = client.get_grid_layout_state("app.center")
            rows = center.get("rows") or []
            if len(rows) > 1:
                current_height = rows[1].get("computed_size", 0)
                if visible:
                    saved_height = _layout_value(app_state.get("bottom_panel_height"), 220)
                    height = saved_height
                elif current_height and current_height >= 120:
                    height = _layout_value(current_height, 220)
                    app_state["bottom_panel_height"] = height
                    _save_ide_state({"layout": {"bottom_height": height}})
        except Exception:
            pass

        try:
            client.set_grid_row_exact_size("app.center", 1, height if visible else 0)
        except Exception:
            try:
                client.call(
                    "ui.setGridRowExactSize",
                    {"target": "app.center", "index": 1, "size": height if visible else 0},
                )
            except Exception:
                pass

        try:
            client.set_visible("bottom.panel", visible)
        except Exception:
            pass

    def _toggle_bottom_panel(client):
        visible = not bool(app_state.get("bottom_panel_visible", False))
        app_state["bottom_panel_visible"] = visible
        _save_ide_state({"ui": {"bottom_panel_visible": visible}})
        _apply_bottom_panel_visibility(client, visible)

    def _set_bottom_view(client, view: str):
        show_terminal = view == "terminal"
        try:
            client.set_visible("bottom.build_output", not show_terminal)
            client.set_visible("bottom.terminal_panel", show_terminal)
            if show_terminal:
                client.set_focus("bottom.terminal_input")
        except Exception:
            pass

    def _terminal_cwd() -> str:
        cwd = terminal_state.get("cwd") or app_state.get("project_root") or os.getcwd()
        try:
            path = Path(cwd).expanduser()
            if path.is_dir():
                return str(path)
        except Exception:
            pass
        return os.getcwd()

    def _terminal_append(client, text: str):
        terminal_state["output"] = (terminal_state.get("output", "") + text)[-24000:]
        try:
            client.set_text("bottom.terminal_output", terminal_state["output"])
        except Exception:
            pass

    def _run_terminal_command(client, command: str):
        command = command.strip()
        cwd = _terminal_cwd()
        terminal_state["cwd"] = cwd

        if not command:
            _terminal_append(client, "\n$ ")
            return

        _terminal_append(client, f"{command}\n")

        if command == "clear":
            terminal_state["output"] = "$ "
            try:
                client.set_text("bottom.terminal_output", terminal_state["output"])
            except Exception:
                pass
            return

        if command.startswith("cd"):
            target = command[2:].strip() or str(Path.home())
            next_cwd = Path(target).expanduser()
            if not next_cwd.is_absolute():
                next_cwd = Path(cwd) / next_cwd
            try:
                resolved = next_cwd.resolve()
                if resolved.is_dir():
                    terminal_state["cwd"] = str(resolved)
                    _terminal_append(client, f"CWD: {terminal_state['cwd']}\n$ ")
                else:
                    _terminal_append(client, f"cd: no such directory: {target}\n$ ")
            except Exception as exc:
                _terminal_append(client, f"cd: {exc}\n$ ")
            return

        def _worker(cmd=command, run_cwd=cwd):
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=run_cwd,
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=120,
                    env=os.environ.copy(),
                )
                output = proc.stdout or ""
                if proc.returncode:
                    output += f"[exit {proc.returncode}]\n"
            except subprocess.TimeoutExpired:
                output = "[command timed out after 120s]\n"
            except Exception as exc:
                output = f"{exc}\n"

            def _update():
                _terminal_append(client, output + "$ ")
            _deferred(_update)

        threading.Thread(target=_worker, daemon=True).start()

    def _refresh_repo_panel(client):
        project_root = app_state.get("project_root", "")
        if not project_root:
            try:
                client.set_text("nav.repo.status", "No project open")
                client.replace_list_items("nav.repo.changes", [])
            except Exception:
                pass
            return
        import subprocess
        try:
            result = subprocess.run(
                ["git", "-C", project_root, "status", "--porcelain"],
                capture_output=True, text=True, timeout=5
            )
            lines = [l for l in result.stdout.splitlines() if l.strip()]
        except Exception:
            lines = []
        items = []
        for line in lines:
            status = line[:2].strip()
            path = line[3:].strip()
            label = f"[{status}]  {path}"
            items.append({"id": f"repo.file.{path}", "label": label})
        summary = f"{len(items)} change{'s' if len(items) != 1 else ''}" if items else "No changes"
        try:
            client.set_text("nav.repo.status", summary)
            client.replace_list_items("nav.repo.changes", items)
        except Exception:
            pass

    ev_state = {"commit_msg": ""}

    def _ev_repo() -> "ProjectRepo | None":
        if not _EV_AVAILABLE:
            return None
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        repo_dir = Path(project_root) / EV_PROJECT_DIR
        if not repo_dir.is_dir():
            return None
        try:
            return ProjectRepo(Path(project_root))
        except Exception:
            return None

    def _refresh_ev_panel(client):
        repo = _ev_repo()
        has_repo = repo is not None
        try:
            client.set_visible("nav.ev_repo_content",    has_repo)
            client.set_visible("nav.ev_tab_setup_panel", not has_repo)
        except Exception:
            pass
        if not has_repo:
            try:
                project_root = app_state.get("project_root", "")
                if project_root and not ev_setup_state.get("name"):
                    suggested = Path(project_root).name
                    ev_setup_state["name"] = suggested
                    client.set_text("nav.ev_tab_setup.name", suggested)
                client.set_text("nav.ev_tab_setup.error", "")
            except Exception:
                pass
            return
        try:
            branch = repo.current_branch()
            status = repo.status()
        except Exception as exc:
            try:
                client.set_text("nav.ev.branch_label", f"Error: {exc}")
                client.set_text("nav.ev.status", "")
                client.replace_list_items("nav.ev.changes", [])
            except Exception:
                pass
            return
        added    = status.get("added", [])
        modified = status.get("modified", [])
        deleted  = status.get("deleted", [])
        total = len(added) + len(modified) + len(deleted)
        summary = f"{total} change{'s' if total != 1 else ''}" if total else "No changes"
        items = []
        for p in added:
            items.append({"id": f"ev.file.{p}", "label": f"A  {p}"})
        for p in modified:
            items.append({"id": f"ev.file.{p}", "label": f"M  {p}"})
        for p in deleted:
            items.append({"id": f"ev.file.{p}", "label": f"D  {p}"})
        try:
            client.set_text("nav.ev.branch_label", f"Branch: {branch}")
            client.set_text("nav.ev.status", summary)
            client.replace_list_items("nav.ev.changes", items)
        except Exception:
            pass

    ev_bugs_state = {"selected_id": None, "filter": "all", "new_title": ""}
    ev_setup_state = {"name": "", "server": "", "remote_root": "", "branch": "main"}

    def _issues_list_items() -> list:
        repo = _ev_repo()
        if not repo:
            return []
        try:
            bugs = repo.list_bugs()
        except Exception:
            return [{"id": "issues.error", "label": "Error loading bugs"}]
        filt = ev_bugs_state.get("filter", "all")
        items = []
        for bug in bugs:
            status = bug.get("status", "open")
            if filt == "open" and status != "open":
                continue
            if filt == "closed" and status != "closed":
                continue
            icon = "✓" if status == "closed" else "●"
            severity = bug.get("severity", "medium")
            bug_id = bug.get("bug_id", "")
            title = bug.get("title", "")
            items.append({"id": f"issue.{bug_id}", "label": f"{icon}  {bug_id}  [{severity}]  {title}"})
        if not items:
            items.append({"id": "issues.empty", "label": f"No {filt if filt != 'all' else ''} bugs".strip()})
        return items

    def _refresh_issues_panel(client):
        repo = _ev_repo()
        has_repo = repo is not None
        try:
            client.set_visible("nav.ev_issues_content", has_repo)
            client.set_visible("nav.ev_setup_panel",    not has_repo)
            if has_repo:
                client.replace_list_items("nav.issues.list", _issues_list_items())
            else:
                project_root = app_state.get("project_root", "")
                if project_root and not ev_setup_state.get("name"):
                    suggested = Path(project_root).name
                    ev_setup_state["name"] = suggested
                    client.set_text("nav.ev_setup.name", suggested)
                client.set_text("nav.ev_setup.error", "")
        except Exception:
            pass

    def _run_search(client, query: str):
        project_root = app_state.get("project_root", "")
        if not query.strip() or not project_root:
            try:
                client.replace_list_items("nav.search.results", [])
            except Exception:
                pass
            return
        import subprocess
        try:
            result = subprocess.run(
                ["grep", "-rn", "--include=*.e", "--include=*.cpp",
                 "--include=*.h", "--include=*.py", "-m", "5",
                 "-F", query, project_root],
                capture_output=True, text=True, timeout=10
            )
            raw_lines = result.stdout.splitlines()[:200]
        except Exception:
            raw_lines = []
        items = []
        for raw in raw_lines:
            parts = raw.split(":", 2)
            if len(parts) >= 3:
                file_path, lineno, text = parts[0], parts[1], parts[2].strip()
                rel = file_path[len(project_root):].lstrip("/")
                label = f"{rel}:{lineno}  {text[:60]}"
                items.append({"id": f"search.result.{file_path}:{lineno}", "label": label})
        try:
            client.replace_list_items("nav.search.results", items)
        except Exception:
            pass

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
                "epa_block_map": {},
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "trace_nodes": None,
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{tab_id}.debug_ghs.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{tab_id}.debug_stack.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{tab_id}.debug_local.root"),
                "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{tab_id}.debug_dynamic.root"),
                "available_types": [],
                "available_workers": [],
                "undo_stack": [],
                "redo_stack": [],
                "last_undo_time": 0.0,
                "in_undo_redo": False,
            }
            _load_history(tab_id)
        else:
            tab_ui = UiDocumentBuilder()
            tab_ui.create_code_editor(tab_id + ".container", source_text)
            tab_ui.set_property_number(tab_id + ".container", "font_size", 13)
            tab_ui.set_property_string(tab_id + ".container", "language", _editor_language_for_path(file_path))
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
            _focus_editor_widget(client, tab_id, editor_state.get(tab_id))
        except Exception:
            pass

    def _ai_format_history(extra_assistant_text=None) -> str:
        """Return JSON-encoded messages for the chat dialog widget."""
        if not ai_state["messages"] and extra_assistant_text is None:
            return json.dumps({"messages": [{"role": "assistant", "display": (
                "How can I help you build today?\n\n"
                "I can help you with E language, EPA assembly, kernel design, "
                "C++ host integration, and project structure.\n\n"
                "Tip: toggle File below to include your open file as context."
            )}]})
        msgs = []
        for msg in ai_state["messages"]:
            if msg["role"] == "user":
                display = msg["content"].split("\n\n---\n\n")[0]
                msgs.append({"role": "user", "display": display})
            else:
                msgs.append({"role": "assistant", "display": msg["content"]})
        if extra_assistant_text is not None:
            msgs.append({"role": "assistant", "display": extra_assistant_text or "..."})
        return json.dumps({"messages": msgs})

    def _ai_build_context() -> str:
        """Return a context block to append to the API user message."""
        sections = []
        if ai_state.get("ctx_file"):
            tid = app_state.get("active_editor_tab", "")
            if tid and tid in editor_state:
                st = editor_state[tid]
                title = st.get("title", "file")
                src = st.get("source_text", "")
                if src:
                    ext = Path(title).suffix.lower()
                    lang = {".e": "e", ".cpp": "cpp", ".h": "cpp", ".py": "python"}.get(ext, "")
                    sections.append(f"Current file: `{title}`\n\n```{lang}\n{src}\n```")
        if ai_state.get("ctx_project"):
            name = app_state.get("project_name", "")
            root = app_state.get("project_root", "")
            if name:
                sections.append(f"Project: **{name}**\nPath: `{root}`")
        return "\n\n".join(sections)

    def _ai_send():
        """Background thread: send user message to the Anthropic API and stream response."""
        message_text = ai_state.get("input_text", "").strip()
        if not message_text:
            return
        c = client_ref.get("client")
        if not c:
            return

        context = _ai_build_context()
        api_content = f"{message_text}\n\n---\n\n{context}" if context else message_text
        ai_state["messages"].append({"role": "user", "content": api_content})
        ai_state["input_text"] = ""

        cancel_event = threading.Event()
        ai_state["cancel_event"] = cancel_event
        ai_state["generating"] = True

        try:
            c.set_text("ai.history", _ai_format_history(extra_assistant_text=""))
            c.set_text("ai.input", "")
            c.call("ui.setVisible", {"target": "ai.stop", "visible": True})
            c.call("ui.setVisible", {"target": "ai.send", "visible": False})
        except Exception:
            pass

        response_text = ""
        try:
            import anthropic as _ant
            aclient = _ant.Anthropic()
            last_update = time.monotonic()
            with aclient.messages.stream(
                model=ai_state.get("model", "claude-sonnet-4-6"),
                max_tokens=4096,
                system=ANTHROPIC_SYSTEM_PROMPT,
                messages=[{"role": m["role"], "content": m["content"]}
                          for m in ai_state["messages"]],
            ) as stream:
                for chunk in stream.text_stream:
                    if cancel_event.is_set():
                        response_text += "\n\n[stopped]"
                        break
                    response_text += chunk
                    now = time.monotonic()
                    if now - last_update >= 0.08:
                        try:
                            c.set_text("ai.history",
                                       _ai_format_history(extra_assistant_text=response_text))
                        except Exception:
                            break
                        last_update = now
        except ImportError:
            response_text = (
                "The `anthropic` library is not installed.\n\n"
                "Run `pip install anthropic` and set the `ANTHROPIC_API_KEY` "
                "environment variable, then restart the IDE."
            )
        except Exception as exc:
            response_text = f"Error communicating with the API: {exc}"

        ai_state["messages"].append({"role": "assistant", "content": response_text})
        ai_state["generating"] = False
        ai_state["cancel_event"] = None

        try:
            c.set_text("ai.history", _ai_format_history())
            c.call("ui.setVisible", {"target": "ai.stop", "visible": False})
            c.call("ui.setVisible", {"target": "ai.send", "visible": True})
        except Exception:
            pass

    def _deferred(fn):
        """Run fn on a thread so on_ui_event returns before any RPC calls are made."""
        threading.Thread(target=fn, daemon=True).start()

    def on_ui_event(params):
        client = client_ref.get("client")
        action = params.get("action")
        payload = params.get("payload") or {}
        payload_action = payload.get("action", "") if isinstance(payload, dict) else ""
        target = params.get("target")
        _push_event("ui_event", action=action, target=target,
                    payload=_trim_for_log(payload) if isinstance(payload, dict) else payload)

        if action == "valueChanged" and target == "editor.tabs" and client is not None:
            new_index = int(payload.get("value", -1))
            for tid, st in editor_state.items():
                entry = next((t for t in tab_list if t.get("tab_id") == tid), None)
                if entry and entry.get("index") == new_index:
                    app_state["active_editor_tab"] = tid
                    c = client
                    _deferred(lambda t=tid, s=st: _focus_editor_widget(c, t, s))
                    break
            return {"received": True}

        for tab_id, state in editor_state.items():
            ids = _editor_ids(tab_id)
            if action == "textChanged" and target == ids["source"]:
                app_state["active_editor_tab"] = tab_id
                prev_text = state.get("source_text", "")
                new_text = payload.get("text", "")
                if state.get("in_undo_redo"):
                    state["in_undo_redo"] = False
                elif prev_text != new_text:
                    now = time.time()
                    last_time = state.get("last_undo_time", 0.0)
                    if now - last_time > 2.0 or not state.get("undo_stack"):
                        state.setdefault("undo_stack", []).append({"text": prev_text})
                        if len(state["undo_stack"]) > 100:
                            state["undo_stack"] = state["undo_stack"][-100:]
                    state["redo_stack"] = []
                    state["last_undo_time"] = now
                state["source_text"] = new_text
                state["compile_seq"] = int(state.get("compile_seq", 0)) + 1
                if client is not None:
                    c = client
                    current_tab = tab_id
                    seq = state["compile_seq"]
                    _deferred(lambda: _refresh_e_tab(c, current_tab, seq))
                return {"received": True}
        # Track technology checkbox toggles from the wizard.
        if action == "valueChanged" and target in (
            "wizard.tech.epa", "wizard.tech.cpp", "wizard.tech.python"
        ):
            key = "tech_" + target.rsplit(".", 1)[-1]
            wizard_state[key] = payload.get("value", 0) > 0.5

        if action == "action" and target == "app.toolbar" and client is not None:
            if not app_state.get("project_root"):
                return {"received": True}
            btn = payload.get("action", "")
            view_map = {
                "toolbar.files":  "files",
                "toolbar.search": "search",
                "toolbar.repo":   "repo",
                "toolbar.issues": "issues",
                "toolbar.debug":  "debug",
            }
            if btn in view_map:
                c = client
                v = view_map[btn]
                _deferred(lambda: _switch_nav_view(c, v))
                return {"received": True}

        if action == "action" and target == "bottom.toolbar" and client is not None:
            btn = payload.get("action", "")
            c = client
            if btn == "bottom.build":
                _deferred(lambda: _set_bottom_view(c, "build"))
                return {"received": True}
            if btn == "bottom.terminal":
                _deferred(lambda: _set_bottom_view(c, "terminal"))
                return {"received": True}
            if btn == "bottom.clear":
                def _clear_bottom():
                    try:
                        c.set_text("bottom.build_output", "")
                        terminal_state["output"] = "$ "
                        c.set_text("bottom.terminal_output", terminal_state["output"])
                    except Exception:
                        pass
                _deferred(_clear_bottom)
                return {"received": True}

        if action == "textChanged" and target == "bottom.terminal_input":
            terminal_state["input"] = payload.get("text", "")
            return {"received": True}

        if action == "keyDown" and target == "bottom.terminal_input" and client is not None:
            keyval = payload.get("keyval", 0)
            if keyval in (0xff0d, 0x0000ff0d, 65293, 13):
                command = terminal_state.get("input", "")
                terminal_state["input"] = ""
                c = client
                def _submit_terminal():
                    try:
                        c.set_text("bottom.terminal_input", "")
                    except Exception:
                        pass
                    _run_terminal_command(c, command)
                _deferred(_submit_terminal)
                return {"received": True}

        if action == "textChanged" and target == "nav.search.input" and client is not None:
            query = payload.get("text", "")
            c = client
            _deferred(lambda q=query: _run_search(c, q))
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_repo_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.stage_all" and client is not None:
            project_root = app_state.get("project_root", "")
            if project_root:
                import subprocess
                c = client
                def _do_stage_all():
                    try:
                        subprocess.run(["git", "-C", project_root, "add", "-A"],
                                       timeout=10, capture_output=True)
                    except Exception:
                        pass
                    _refresh_repo_panel(c)
                _deferred(_do_stage_all)
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.commit" and client is not None:
            project_root = app_state.get("project_root", "")
            if project_root:
                import subprocess
                c = client
                commit_msg = app_state.get("repo_commit_msg", "").strip()
                def _do_commit(msg=commit_msg):
                    if not msg:
                        return
                    try:
                        subprocess.run(
                            ["git", "-C", project_root, "commit", "-m", msg],
                            timeout=15, capture_output=True
                        )
                        app_state["repo_commit_msg"] = ""
                        try:
                            c.set_text("nav.repo.commit_msg", "")
                        except Exception:
                            pass
                    except Exception:
                        pass
                    _refresh_repo_panel(c)
                _deferred(_do_commit)
            return {"received": True}

        if action == "textChanged" and target == "nav.repo.commit_msg":
            app_state["repo_commit_msg"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev.commit_msg":
            ev_state["commit_msg"] = payload.get("text", "")
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_ev_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.commit" and client is not None:
            msg = ev_state.get("commit_msg", "").strip()
            c = client
            def _do_ev_commit(m=msg):
                repo = _ev_repo()
                if not repo or not m:
                    return
                try:
                    repo.commit(m)
                    ev_state["commit_msg"] = ""
                    try:
                        c.set_text("nav.ev.commit_msg", "")
                    except Exception:
                        pass
                except Exception as exc:
                    print(f"[ev.commit] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_commit)
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.push" and client is not None:
            c = client
            def _do_ev_push():
                repo = _ev_repo()
                if not repo:
                    return
                try:
                    result = repo.push_current_tree()
                    print(json.dumps({"ev.push": result}, indent=2), flush=True)
                except Exception as exc:
                    print(f"[ev.push] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_push)
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.pull" and client is not None:
            c = client
            def _do_ev_pull():
                repo = _ev_repo()
                if not repo:
                    return
                try:
                    result = repo.pull_current_tree()
                    print(json.dumps({"ev.pull": result}, indent=2), flush=True)
                except Exception as exc:
                    print(f"[ev.pull] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_pull)
            return {"received": True}

        if action == "action" and payload.get("action") == "issues.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_issues_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action", "").startswith("issues.filter.") and client is not None:
            filt = payload.get("action")[len("issues.filter."):]
            ev_bugs_state["filter"] = filt
            c = client
            _deferred(lambda: c.replace_list_items("nav.issues.list", _issues_list_items()))
            return {"received": True}

        if action == "action" and payload.get("action") == "issues.add" and client is not None:
            title = ev_bugs_state.get("new_title", "").strip()
            if title:
                repo = _ev_repo()
                if repo:
                    try:
                        import getpass
                        author = getpass.getuser()
                    except Exception:
                        author = "user"
                    try:
                        repo.create_bug(title, severity="medium", author=author)
                    except Exception:
                        pass
                ev_bugs_state["new_title"] = ""
                c = client
                def _after_add():
                    try:
                        c.set_text("nav.issues.new_title", "")
                        c.replace_list_items("nav.issues.list", _issues_list_items())
                    except Exception:
                        pass
                _deferred(_after_add)
            return {"received": True}

        if action == "textChanged" and target == "nav.issues.new_title":
            ev_bugs_state["new_title"] = payload.get("text", "")
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.issues.list":
            sel = payload.get("action") or payload.get("id", "")
            if sel.startswith("issue."):
                ev_bugs_state["selected_id"] = sel[len("issue."):]
            return {"received": True}

        if action == "action" and payload.get("action") in ("issues.close", "issues.reopen") and client is not None:
            sel_id = ev_bugs_state.get("selected_id")
            if sel_id and not sel_id.startswith("issues."):
                repo = _ev_repo()
                if repo:
                    new_status = "closed" if payload.get("action") == "issues.close" else "open"
                    try:
                        repo.update_bug(sel_id, status=new_status)
                    except Exception:
                        pass
                c = client
                _deferred(lambda: c.replace_list_items("nav.issues.list", _issues_list_items()))
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.name":
            ev_setup_state["name"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.server":
            ev_setup_state["server"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.remote_root":
            ev_setup_state["remote_root"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.branch":
            ev_setup_state["branch"] = payload.get("text", "") or "main"
            return {"received": True}

        if action in ("action",) and payload.get("action") in ("ev.setup.init", "ev.tab.setup.init") and client is not None:
            from_tab = payload.get("action") == "ev.tab.setup.init"
            error_target = "nav.ev_tab_setup.error" if from_tab else "nav.ev_setup.error"
            if from_tab:
                name        = ev_setup_state.get("tab_name",        ev_setup_state.get("name", "")).strip()
                server      = ev_setup_state.get("tab_server",      ev_setup_state.get("server", "")).strip()
                remote_root = ev_setup_state.get("tab_remote_root", ev_setup_state.get("remote_root", "")).strip()
                branch      = (ev_setup_state.get("tab_branch",     ev_setup_state.get("branch", "main")).strip() or "main")
            else:
                name        = ev_setup_state.get("name", "").strip()
                server      = ev_setup_state.get("server", "").strip()
                remote_root = ev_setup_state.get("remote_root", "").strip()
                branch      = ev_setup_state.get("branch", "main").strip() or "main"
            project_root = app_state.get("project_root", "")
            c = client
            if not project_root:
                return {"received": True}
            if not name:
                _deferred(lambda: c.set_text(error_target, "Project name is required."))
                return {"received": True}
            if not _EV_AVAILABLE or ProjectRepo is None:
                _deferred(lambda: c.set_text(error_target, "EV module not available."))
                return {"received": True}
            err_tgt = error_target
            def _do_init():
                try:
                    ProjectRepo.init_project(
                        Path(project_root),
                        name=name,
                        server=server,
                        remote_root=remote_root or "/",
                        branch=branch,
                    )
                    _refresh_issues_panel(c)
                    _refresh_ev_panel(c)
                except Exception as exc:
                    try:
                        c.set_text(err_tgt, str(exc)[:100])
                    except Exception:
                        pass
            _deferred(_do_init)
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.name":
            ev_setup_state["tab_name"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.server":
            ev_setup_state["tab_server"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.remote_root":
            ev_setup_state["tab_remote_root"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.branch":
            ev_setup_state["tab_branch"] = payload.get("text", "") or "main"
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.search.results" and client is not None:
            result_id = payload.get("action") or payload.get("id", "")
            if result_id.startswith("search.result."):
                loc = result_id[len("search.result."):]
                parts = loc.rsplit(":", 1)
                file_path = parts[0]
                if Path(file_path).is_file():
                    c = client
                    fp = file_path
                    _deferred(lambda: _open_file_tab(c, fp, True))
            return {"received": True}

        if action == "valueChanged" and target == "wizard.python.multi_cpu":
            wizard_state["python_multi_cpu"] = payload.get("value", 0) > 0.5

        if action == "valueChanged" and target == "wizard.cpp.epa_vm_host":
            wizard_state["cpp_epa_vm_host"] = payload.get("value", 0) > 0.5

        if action == "valueChanged" and target == "wizard.cpp.epa_debug_rpc":
            wizard_state["cpp_epa_debug_rpc"] = payload.get("value", 0) > 0.5

        if target in ("wizard.ui_client", "wizard.ui_template") and action in ("action", "valueChanged", "clicked"):
            selected = payload.get("action") or payload.get("id") or payload.get("text") or ""
            if target == "wizard.ui_client" and selected in ("both", "cpp", "python"):
                wizard_state["ui_client"] = selected
                if selected == "cpp":
                    wizard_state["tech_cpp"] = True
                    wizard_state["tech_python"] = False
                elif selected == "python":
                    wizard_state["tech_cpp"] = False
                    wizard_state["tech_python"] = True
                else:
                    wizard_state["tech_cpp"] = True
                    wizard_state["tech_python"] = True
            if target == "wizard.ui_template" and selected in ("tabbed-control-panel", "rich-editor"):
                wizard_state["ui_template"] = selected

        if action == "textChanged" and target in ("wizard.project_name", "wizard.rpc_host", "wizard.rpc_port"):
            key = {
                "wizard.project_name": "project_name",
                "wizard.rpc_host": "rpc_host",
                "wizard.rpc_port": "rpc_port",
            }[target]
            wizard_state[key] = payload.get("text", "")

        # Track project name typed into the wizard text input.
        if action == "keysTyped" and target == "wizard.project_name":
            wizard_state["project_name"] = wizard_state.get("project_name", "") + payload.get("text", "")

        if action == "keyDown" and target == "wizard.project_name":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = wizard_state.get("project_name", "")
                if name:
                    wizard_state["project_name"] = name[:-1]

        if action == "keysTyped" and target in ("wizard.rpc_host", "wizard.rpc_port"):
            key = "rpc_host" if target == "wizard.rpc_host" else "rpc_port"
            wizard_state[key] = wizard_state.get(key, "") + payload.get("text", "")

        if action == "keyDown" and target in ("wizard.rpc_host", "wizard.rpc_port"):
            if payload.get("keyval", 0) == 0xff08:
                key = "rpc_host" if target == "wizard.rpc_host" else "rpc_port"
                value = wizard_state.get(key, "")
                if value:
                    wizard_state[key] = value[:-1]

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

        # Open-file dialog: double-click a directory to navigate into it.
        if target == "dialog.files" and action == "action":
            entry_path = payload.get("action", "")
            if entry_path and Path(entry_path).is_dir() and client is not None:
                c = client
                p = entry_path
                _deferred(lambda: _open_file_navigate(c, p))
            return {"received": True}

        # Open-file dialog: track typed location bar changes.
        if action == "textChanged" and target == "dialog.location":
            typed = payload.get("text", "")
            if typed and Path(typed).is_dir():
                open_file_nav_state["path"] = typed

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
                    _deferred(lambda: (_apply_editor_view(c, current_tab, set_focus=True), _refresh_debug_sidebars(c, current_tab)))
                    return {"received": True}
                if item_action == ids["button_epa"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "epa"
                    current_tab = tab_id
                    _deferred(lambda: _refresh_e_tab(c, current_tab, focus=True))
                    return {"received": True}
            if item_action and item_action.startswith("tab.close."):
                close_tab_id = item_action[len("tab.close."):]
                entry = next((t for t in tab_list if t["tab_id"] == close_tab_id), None)
                if entry:
                    close_index = entry["index"]
                    tab_list.remove(entry)
                    if close_tab_id in editor_state:
                        _save_history(close_tab_id)
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
                "edit.undo", "edit.redo"
            ):
                _undo_action = item_action
                def _do_undo_redo(ua=_undo_action):
                    tab_id = app_state.get("active_editor_tab", "")
                    state = editor_state.get(tab_id) if tab_id else None
                    if not state:
                        return
                    ids = _editor_ids(tab_id)
                    undo_stack = state.setdefault("undo_stack", [])
                    redo_stack = state.setdefault("redo_stack", [])
                    current_text = state.get("source_text", "")
                    if ua == "edit.undo":
                        if not undo_stack:
                            return
                        entry = undo_stack.pop()
                        redo_stack.append({"text": current_text})
                        restored = entry["text"]
                    else:
                        if not redo_stack:
                            return
                        entry = redo_stack.pop()
                        undo_stack.append({"text": current_text})
                        restored = entry["text"]
                    state["in_undo_redo"] = True
                    try:
                        c.set_text(ids["source"], restored)
                    except Exception:
                        state["in_undo_redo"] = False
                _deferred(_do_undo_redo)
                return {"received": True}

            if target == "app.menu" and item_action in (
                "edit.cut", "edit.copy", "edit.paste", "edit.select_all"
            ):
                _action = item_action
                def _do_edit_action(action=_action):
                    # Try the currently focused widget first so output panels,
                    # debug views, etc. get copy/paste without focus being stolen.
                    try:
                        result = c.perform_focused_action(action)
                        if isinstance(result, dict) and result.get("dispatched"):
                            return
                    except Exception:
                        pass
                    # Fall back to the active editor when nothing else handled it.
                    tab_id = app_state.get("active_editor_tab", "")
                    state = editor_state.get(tab_id) if tab_id else None
                    if state:
                        ids = _editor_ids(tab_id)
                        view = state.get("view", "e")
                        target_widget = ids["source"]
                        if view == "epa":
                            target_widget = ids["epa"]
                        try:
                            c.set_focus(target_widget)
                            c.perform_action(target_widget, action)
                        except Exception:
                            pass
                _deferred(_do_edit_action)

            if item_action == "app.toggle_theme":
                current_theme = app_state.get("theme", "dark")
                next_theme = "light" if current_theme == "dark" else "dark"
                app_state["theme"] = next_theme
                _deferred(lambda t=next_theme: c.set_theme_mode(t))
                return {"received": True}

            if item_action == "app.toggle_right_panel":
                _deferred(lambda: _toggle_right_panel(c))
                return {"received": True}

            if item_action == "app.toggle_bottom_panel":
                _deferred(lambda: _toggle_bottom_panel(c))
                return {"received": True}

            if target == "app.menu" and item_action == "view.appearance.toggle_window_header":
                next_use_system = not _use_system_window_header()
                _save_ide_state({
                    "ui": {
                        "use_system_window_header": next_use_system,
                    }
                })
                current_title = app_state.get("project_name")
                if current_title:
                    current_title = f"EPA-IDE : {current_title}"
                else:
                    current_title = "EPA-IDE"
                _deferred(lambda: (
                    c.set_window_decorated(next_use_system),
                    c.configure_menu_bar_chrome(
                        "app.menu",
                        custom_chrome=not next_use_system,
                        window_title=current_title,
                    ),
                ))
                return {"received": True}

            if target == "app.menu" and item_action == "file.open":
                saved = _load_ide_state().get("last_open_dir", "")
                initial = saved if saved and Path(saved).is_dir() else str(Path.home())
                open_file_nav_state["path"] = initial
                _deferred(lambda: c.open_window("open-file", "Open File", 920, 640, build_open_file_dialog(initial)))

            elif item_action in ("file.new_project", "no_project.new_project"):
                initial = str(Path.home())
                wizard_state.clear()
                wizard_state.update({
                    "tech_epa": True,
                    "tech_cpp": True,
                    "tech_python": True,
                    "project_name": "",
                    "ui_client": "both",
                    "ui_template": "tabbed-control-panel",
                    "python_multi_cpu": False,
                    "cpp_epa_vm_host": False,
                    "cpp_epa_debug_rpc": True,
                    "rpc_host": "127.0.0.1",
                    "rpc_port": "18777",
                })
                nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "new-project", "New Project", 540, 760,
                    build_new_project_wizard(initial)
                ))

            elif item_action in ("file.open_project", "no_project.open_project"):
                last = _load_ide_state().get("last_project", "")
                initial = str(Path(last).parent) if last and Path(last).parent.is_dir() else str(Path.home())
                open_project_nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "open-project", "Open Project", 500, 500,
                    build_open_project_dialog(initial)
                ))

            elif item_action == "nav.refresh":
                project_root = app_state.get("project_root", "")
                if project_root and client is not None:
                    _deferred(lambda: _open_project(c, project_root))

            elif item_action == "build.build_project":
                _deferred(lambda: _build_project(c, rebuild=False))

            elif item_action == "build.rebuild_project":
                _deferred(lambda: _build_project(c, rebuild=True))

            elif item_action == "build.clean":
                _deferred(_clean_project)

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
                    project_root = app_state.get("project_root", "")
                    if project_root:
                        _open_project(c, project_root)

                _deferred(_do_create_file)

            elif item_action == "dialog.file.confirm":
                current_dir = open_file_nav_state.get("path", "")
                if current_dir:
                    _save_ide_state({"last_open_dir": current_dir})
                _deferred(lambda: c.close_window("open-file"))

            elif item_action == "dialog.file.cancel":
                _deferred(lambda: c.close_window("open-file"))

            elif item_action == "dialog.nav.up":
                current = open_file_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _open_file_navigate(c, p))

            elif item_action == "dialog.nav.back":
                pass  # history not yet tracked

            elif item_action == "dialog.nav.forward":
                pass  # history not yet tracked

            elif item_action == "dialog.folder.refresh":
                current = open_file_nav_state.get("path", str(Path.home()))
                _deferred(lambda: _open_file_navigate(c, current))

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
                ui_template = wizard_state.get("ui_template", "tabbed-control-panel")
                rpc_host = wizard_state.get("rpc_host", "127.0.0.1").strip() or "127.0.0.1"
                rpc_port_text = wizard_state.get("rpc_port", "18777").strip() or "18777"
                python_multi_cpu = bool(wizard_state.get("python_multi_cpu", False))
                cpp_epa_vm_host = bool(wizard_state.get("cpp_epa_vm_host", False))
                cpp_epa_debug_rpc = bool(wizard_state.get("cpp_epa_debug_rpc", True))
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

                    if cpp_epa_debug_rpc and not cpp_epa_vm_host:
                        try:
                            c.set_text("wizard.error", "EPA debug JSON-RPC requires the EPA VM Host adapter.")
                        except Exception:
                            pass
                        return

                    try:
                        rpc_port = int(rpc_port_text)
                    except ValueError:
                        try:
                            c.set_text("wizard.error", "UI RPC port must be an integer.")
                        except Exception:
                            pass
                        return

                    if rpc_port <= 0 or rpc_port > 65535:
                        try:
                            c.set_text("wizard.error", "UI RPC port must be between 1 and 65535.")
                        except Exception:
                            pass
                        return

                    project_root = Path(save_dir) / project_name
                    try:
                        (project_root / ".elaraproject").mkdir(parents=True, exist_ok=True)
                        if tech_epa:
                            (project_root / "epa").mkdir(exist_ok=True)
                        (project_root / "build").mkdir(exist_ok=True)
                        if tech_epa:
                            (project_root / "build" / "epa").mkdir(exist_ok=True)
                        techs = [t for t, v in [("epa", tech_epa), ("cpp", tech_cpp), ("python", tech_python)] if v]
                        (project_root / ".elaraproject" / "project.json").write_text(
                            json.dumps({
                                "name": project_name,
                                "technologies": techs,
                                "ui_template": ui_template,
                                "rpc_host": rpc_host,
                                "rpc_port": rpc_port,
                                "python_multi_cpu": python_multi_cpu,
                                "cpp_epa_vm_host": cpp_epa_vm_host,
                                "cpp_epa_debug_rpc": cpp_epa_debug_rpc,
                                "created": datetime.datetime.utcnow().isoformat() + "Z",
                            }, indent=2),
                            encoding="utf-8",
                        )
                        (project_root / ".elaraproject" / "bookmarks.json").write_text("[]", encoding="utf-8")
                        (project_root / ".elaraproject" / "breakpoints.json").write_text("[]", encoding="utf-8")

                        if tech_cpp:
                            builder = _ensure_project_builder()
                            cpp_root = project_root / "cpp"
                            env = os.environ.copy()
                            env["LC_ALL"] = "C"
                            subprocess.run(
                                [
                                    str(builder),
                                    "--non-interactive",
                                    "--app-kind", "ui",
                                    "--ui-client-language", "cpp",
                                    "--ui-template", ui_template,
                                    "--epa-vm-host", "yes" if cpp_epa_vm_host else "no",
                                    "--epa-debug-rpc", "yes" if (tech_epa and cpp_epa_vm_host and cpp_epa_debug_rpc) else "no",
                                    "--address", rpc_host,
                                    "--port", str(rpc_port),
                                    "--name", project_name,
                                    "--output", str(cpp_root),
                                ],
                                check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=True,
                                env=env,
                            )

                        if tech_python:
                            builder = _ensure_project_builder()
                            python_root = project_root / "python"
                            env = os.environ.copy()
                            env["LC_ALL"] = "C"
                            subprocess.run(
                                [
                                    str(builder),
                                    "--non-interactive",
                                    "--app-kind", "ui",
                                    "--ui-client-language", "python",
                                    "--ui-template", ui_template,
                                    "--multi-cpu-python", "yes" if python_multi_cpu else "no",
                                    "--address", rpc_host,
                                    "--port", str(rpc_port),
                                    "--name", project_name,
                                    "--output", str(python_root),
                                ],
                                check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=True,
                                env=env,
                            )
                    except OSError as exc:
                        try:
                            c.set_text("wizard.error", f"Could not create project: {exc}")
                        except Exception:
                            pass
                        return
                    except subprocess.CalledProcessError as exc:
                        message = (exc.stderr or exc.stdout or str(exc)).strip()
                        try:
                            c.set_text("wizard.error", f"Could not generate project scaffold: {message}")
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

            elif item_action and (
                item_action.startswith("debug.kernel.run.")
                or item_action.startswith("debug.kernel.step.")
            ):
                is_run = item_action.startswith("debug.kernel.run.")
                kernel_id_str = (
                    item_action[len("debug.kernel.run."):]
                    if is_run
                    else item_action[len("debug.kernel.step."):]
                )
                project_root_str = app_state.get("project_root", "")
                if project_root_str:
                    rel = kernel_id_str.replace(".", "/") + ".e"
                    file_path = str(Path(project_root_str) / "epa" / rel)
                    tab_id = _tab_id_for_path(file_path)
                    source_tab_id = tab_id
                    def _open_kernel_tab(fp=file_path, stid=source_tab_id):
                        _open_file_tab(c, fp, make_permanent=True)
                        st = editor_state.get(stid)
                        if st and not st.get("debug", False):
                            st["debug"] = True
                            _apply_editor_view(c, stid)
                            _refresh_debug_sidebars(c, stid)
                        try:
                            c.set_eip_line(_editor_ids(stid)["source"], 0)
                        except Exception:
                            pass
                    _deferred(_open_kernel_tab)

                    bundle_path = _bundle_path_for_kernel_id(kernel_id_str)
                    worker_wid = _selected_worker_wid_for_kernel(kernel_id_str)
                    if worker_wid is None:
                        worker_wid = 1

                    kid = 0
                    do_run = is_run
                    ui_client = c
                    tab_id_for_ind = kernel_id_str
                    def _dbg_run_or_step(bp=bundle_path, r=do_run, k=kid,
                                         uc=ui_client, wid=worker_wid,
                                         ktid=tab_id_for_ind):
                        dbg_c = _epa_dbg_client()
                        if not _epa_dbg_running() or not dbg_c:
                            if not _start_debug_vm(uc, force_restart=False):
                                return
                            dbg_c = _epa_dbg_client()
                        already_loaded = app_state.get("debug_kernel_loaded") == bp
                        if r or not already_loaded or not dbg_c:
                            prev_ktid = _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))
                            if prev_ktid and prev_ktid != ktid and uc:
                                _clear_kernel_queue_state(uc, prev_ktid)
                                _set_kernel_run_indicator(uc, prev_ktid, _IND_OFF)
                            result = _epa_dbg_load_bundle(bp, kernel_id=k)
                            if not result.get("ok"):
                                _epa_dbg_log(f"[error] load_bundle failed: {result.get('error', 'unknown')}")
                                _set_kernel_load_indicator(uc, ktid, _IND_RED)
                                return
                            app_state["debug_kernel_loaded"] = bp
                            _set_kernel_load_indicator(uc, ktid, _IND_GREEN)
                            dbg_c = _epa_dbg_client()
                        if not dbg_c:
                            return
                        # Mark as running before the call
                        if uc:
                            _set_kernel_run_indicator(uc, ktid, _IND_RED)
                        try:
                            if r:
                                resp = dbg_c.run_to_wait(wid=wid)
                            else:
                                resp = dbg_c.step(k, ticks=1)
                            snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
                            if snap and uc:
                                _update_queue_counters(uc, snap)
                                _update_kernel_indicator_from_snapshot(uc, ktid, snap)
                        except Exception as exc:
                            if uc:
                                _set_kernel_run_indicator(uc, ktid, _IND_OFF)
                            _epa_dbg_log(f"[error] dbg_run_or_step: {exc}")
                            _push_exception(exc, "dbg_run_or_step")
                    _deferred(_dbg_run_or_step)
                return {"received": True}

            elif item_action == "ai.send":
                if not ai_state.get("generating") and ai_state.get("input_text", "").strip():
                    _deferred(_ai_send)
                return {"received": True}

            elif item_action == "ai.stop":
                ev = ai_state.get("cancel_event")
                if ev:
                    ev.set()
                return {"received": True}

            elif item_action == "ai.new_chat":
                ai_state["messages"].clear()
                ai_state["input_text"] = ""
                def _do_clear_chat():
                    try:
                        c.set_text("ai.history", _ai_format_history())
                        c.set_text("ai.input", "")
                    except Exception:
                        pass
                _deferred(_do_clear_chat)
                return {"received": True}

        # AI context checkbox toggles
        if action == "valueChanged" and target in (
            "ai.ctx.file", "ai.ctx.project", "ai.ctx.selection"
        ):
            key = "ctx_" + target.rsplit(".", 1)[-1]
            ai_state[key] = payload.get("value", 0) > 0.5
            return {"received": True}

        # Ingress designer — kernel selection
        if target == "nav.debug.ingress_kernel" and action in ("action", "valueChanged", "clicked") and client is not None:
            kernel_id = payload.get("action") or ""
            app_state["debug_ingress_kernel"] = kernel_id
            app_state["debug_ingress_worker"] = ""
            c = client
            def _on_kernel_selected(kid=kernel_id):
                _refresh_ingress_worker_combo(c, kid)
                items = _ingress_types_from_project(kid, "")
                app_state["debug_ingress_types_cache"] = items
                _apply_ingress_types_combo(c, items)
            _deferred(_on_kernel_selected)
            return {"received": True}

        # Ingress designer — worker selection
        if target == "nav.debug.ingress_worker" and action in ("action", "valueChanged", "clicked") and client is not None:
            worker_name = payload.get("action") or ""
            app_state["debug_ingress_worker"] = worker_name
            kernel_id = app_state.get("debug_ingress_kernel", "")
            c = client
            def _on_worker_selected(kid=kernel_id, wname=worker_name):
                items = _ingress_types_from_project(kid, wname)
                app_state["debug_ingress_types_cache"] = items
                _apply_ingress_types_combo(c, items)
            _deferred(_on_worker_selected)
            return {"received": True}

        # Debug VM global controls
        if action == "action" and (
            target in ("nav.debug.vm_reset", "debug.vm.reset")
            or payload_action == "debug.vm.reset"
        ):
            c = client
            def _do_vm_reset():
                app_state.pop("debug_kernel_loaded", None)
                app_state["debug_vm_started"] = False
                if not _start_debug_vm(c, force_restart=True):
                    _epa_dbg_set_vm_status("error", "start failed")
            _deferred(_do_vm_reset)
            return {"received": True}

        if action == "action" and (
            target in ("nav.debug.vm_stop", "debug.vm.stop")
            or payload_action == "debug.vm.stop"
        ):
            c = client
            def _do_vm_stop():
                app_state.pop("debug_kernel_loaded", None)
                app_state["debug_vm_started"] = False
                _reset_all_kernel_debug_state(c)
                _epa_dbg_stop()
            _deferred(_do_vm_stop)
            return {"received": True}

        # Error dialog close button
        if item_action == "error_dialog.close":
            c = client
            _deferred(lambda: c.close_window("epa-dbg-error") if c else None)
            return {"received": True}

        # Kernel step worker combo — track selection per kernel
        if action in ("action", "valueChanged") and target and target.endswith(".worker"):
            prefix = "nav.debug.kernel."
            suffix = ".worker"
            if target.startswith(prefix) and target.endswith(suffix):
                kernel_id_str = target[len(prefix):-len(suffix)]
                worker_name = payload.get("action") or payload.get("id") or ""
                app_state[f"debug_kernel_worker_{kernel_id_str}"] = worker_name
                if client:
                    _apply_cached_kernel_queue_state(client, kernel_id_str)
                    snapshot = app_state.get("debug_kernel_snapshot_state", {}).get(kernel_id_str)
                    if snapshot:
                        _update_kernel_indicator_from_snapshot(client, kernel_id_str, snapshot)
                return {"received": True}

        # Ingress designer — profile selected in list
        if target == "nav.debug.ingress_profiles":
            if action == "valueChanged":
                # List view reports selection as a float index — look up in cached items
                idx = int(payload.get("value", 0))
                items = app_state.get("debug_ingress_profiles_cache", [])
                if 0 <= idx < len(items):
                    app_state["debug_ingress_selected_profile"] = items[idx]["id"]
            elif action in ("action", "clicked"):
                profile_id = payload.get("action") or payload.get("id") or ""
                if profile_id:
                    app_state["debug_ingress_selected_profile"] = profile_id
            return {"received": True}

        # Ingress designer — type selection → refresh profiles list
        if target == "nav.debug.ingress_type" and action in ("action", "valueChanged", "clicked") and client is not None:
            type_name = payload.get("action") or ""
            app_state["debug_ingress_type"] = type_name
            app_state["debug_ingress_selected_profile"] = ""
            c = client
            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(c, tn))
            return {"received": True}

        # Ingress designer — add profile button
        if target == "nav.debug.ingress_add_btn" and action in ("action", "clicked") and client is not None:
            type_name = app_state.get("debug_ingress_type", "")
            if not type_name:
                cached = app_state.get("debug_ingress_types_cache") or []
                type_name = cached[0]["id"] if cached else ""
            c = client
            if type_name:
                _deferred(lambda tn=type_name: _open_ingress_profile_editor(c, tn))
            return {"received": True}

        # Queue selected ingress packet into epa-dbg
        if target == "nav.debug.ingress_queue_btn" and action in ("action", "clicked"):
            selected_profile = app_state.get("debug_ingress_selected_profile", "")
            type_name = app_state.get("debug_ingress_type", "")
            if not type_name:
                cached = app_state.get("debug_ingress_types_cache") or []
                type_name = cached[0]["id"] if cached else ""
            kernel_id = app_state.get("debug_ingress_kernel", "")
            sel_worker = app_state.get("debug_ingress_worker", "")
            c = client
            def _do_queue(tn=type_name, pn=selected_profile, kid=kernel_id, sw=sel_worker):
                if not tn or not pn or not kid:
                    return
                profiles_dir = _profiles_dir(tn)
                if not profiles_dir:
                    return
                profile_path = profiles_dir / f"{pn}.json"
                if not profile_path.is_file():
                    return
                try:
                    profile_data = json.loads(profile_path.read_text(encoding="utf-8"))
                except Exception:
                    return
                field_values = profile_data.get("fields", profile_data)
                # Encode fields as little-endian uint32 words (little-endian)
                import struct as _struct
                parts = []
                for v in field_values.values():
                    try:
                        parts.append(int(v, 0) if isinstance(v, str) else int(v))
                    except Exception:
                        parts.append(0)
                hex_bytes = "".join(_struct.pack("<I", p & 0xFFFFFFFF).hex() for p in parts)
                dbg_c = _epa_dbg_client()
                if not _epa_dbg_running() or not dbg_c:
                    if not _start_debug_vm(c, force_restart=False):
                        return
                    dbg_c = _epa_dbg_client()
                if not dbg_c:
                    return
                bundle_path = _bundle_path_for_kernel_id(kid)
                if not bundle_path or not Path(bundle_path).is_file():
                    return
                if app_state.get("debug_kernel_loaded") != bundle_path:
                    previously_loaded = _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))
                    if previously_loaded and previously_loaded != kid and c:
                        _clear_kernel_queue_state(c, previously_loaded)
                        _set_kernel_run_indicator(c, previously_loaded, _IND_OFF)
                    result = _epa_dbg_load_bundle(bundle_path, kernel_id=0)
                    if not result.get("ok"):
                        err_msg = result.get("error", "failed to load debug bundle")
                        _epa_dbg_log(f"[error] load_bundle failed: {err_msg}")
                        _set_kernel_load_indicator(c, kid, _IND_RED)
                        raise RuntimeError(err_msg)
                    app_state["debug_kernel_loaded"] = bundle_path
                    _set_kernel_load_indicator(c, kid, _IND_GREEN)
                # Resolve wid from selected worker name (wid=0 is coordinator, workers start at 1)
                workers = app_state.get(f"debug_kernel_workers_{kid}", [])
                wid = 1  # default to first worker
                if sw:
                    for idx, w in enumerate(workers):
                        if w.get("name") == sw:
                            wid = idx + 1
                            break
                try:
                    dbg_c.ingress_push_hex(hex_bytes, wid=wid, path_id=kid)
                    snap = dbg_c.snapshot(0, path_id=kid)
                    if c and kid:
                        _update_kernel_queue_state_from_snapshot(c, kid, snap)
                        _update_kernel_indicator_from_snapshot(c, kid, snap)
                        _update_eip_marker(c, kid, snap)
                except Exception as exc:
                    _epa_dbg_log(f"[error] queue_ingress: {exc}")
                    _push_exception(exc, "queue_ingress")
            _deferred(_do_queue)
            return {"received": True}

        # Profile editor — field selected in tree
        if target == "ipe.tree" and action == "action" and client is not None:
            node_id = payload.get("action") or ""
            if node_id.startswith("ipe.field."):
                field_name = node_id[len("ipe.field."):]
                ingress_editor_state["selected_field"] = field_name
                cur_val = ingress_editor_state.get("field_values", {}).get(field_name, "0")
                ingress_editor_state["field_input_text"] = cur_val
                c = client
                def _update_form(fname=field_name, fval=cur_val):
                    try:
                        c.set_text("ipe.field_label", fname)
                        c.set_text("ipe.field_input", fval)
                    except Exception:
                        pass
                _deferred(_update_form)
            return {"received": True}

        # Profile editor — field input events (keysTyped fires; textChanged is fallback)
        if target == "ipe.field_input":
            field_name = ingress_editor_state.get("selected_field", "")
            if action == "keysTyped":
                cur = ingress_editor_state.get("field_input_text", "")
                new_text = cur + payload.get("text", "")
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            elif action == "keyDown" and payload.get("key") == "Backspace":
                cur = ingress_editor_state.get("field_input_text", "")
                new_text = cur[:-1] if cur else ""
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            elif action == "textChanged":
                new_text = payload.get("text", "")
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            return {"received": True}

        # Profile editor — name input events (text read via snapshot_widget at save time)
        if target == "ipe.name_input":
            return {"received": True}

        # Profile editor — save
        if target == "ipe.save" and action in ("action", "clicked") and client is not None:
            type_name = ingress_editor_state.get("type_name", "")
            c = client
            def _do_save(tn=type_name):
                # Read widget text directly — text input events don't fire from secondary windows
                try:
                    name_snap = c.snapshot_widget("ipe.name_input")
                    pn = ((name_snap or {}).get("state") or {}).get("text", "").strip()
                except Exception:
                    pn = ""
                # Flush current field_input value into field_values for whichever field is selected
                sel_field = ingress_editor_state.get("selected_field", "")
                if sel_field:
                    try:
                        fi_snap = c.snapshot_widget("ipe.field_input")
                        fi_val = ((fi_snap or {}).get("state") or {}).get("text", "")
                        ingress_editor_state.setdefault("field_values", {})[sel_field] = fi_val
                    except Exception:
                        pass
                fv = dict(ingress_editor_state.get("field_values", {}))
                _epa_dbg_log(f"[ipe.save] type={tn!r} name={pn!r} fields={list(fv.keys())}")
                if not tn:
                    _epa_dbg_log("[ipe.save] no type_name — aborting")
                    return
                if not pn:
                    _epa_dbg_log("[ipe.save] empty profile name — showing error")
                    try:
                        c.set_text("ipe.name_label", "Profile name (required):")
                        c.call("ui.setForegroundColor", {"target": "ipe.name_label", "color": "#cc3333"})
                    except Exception as exc:
                        _epa_dbg_log(f"[ipe.save] label update failed: {exc}")
                    return
                d = _profiles_dir(tn)
                if d is None:
                    _epa_dbg_log("[ipe.save] no project open — cannot save profile")
                    try:
                        c.set_text("ipe.name_label", "Open a project first:")
                        c.call("ui.setForegroundColor", {"target": "ipe.name_label", "color": "#cc3333"})
                    except Exception:
                        pass
                    return
                _save_ingress_profile(tn, pn, fv)
                _epa_dbg_log(f"[ipe.save] saved profile {pn!r} for type {tn!r}")
                try:
                    c.close_window("ingress-profile-editor")
                except Exception:
                    pass
                _refresh_ingress_profiles_list(c, tn)
            _deferred(_do_save)
            return {"received": True}

        # Profile editor — cancel
        if target == "ipe.cancel" and action in ("action", "clicked") and client is not None:
            c = client
            _deferred(lambda: c.close_window("ingress-profile-editor"))
            return {"received": True}

        # AI model selection (combo box fires action event with item id)
        if target == "ai.model" and action in ("action", "valueChanged", "clicked"):
            model_id = payload.get("action") or payload.get("id") or payload.get("text")
            if model_id and any(m["id"] == model_id for m in AI_MODELS):
                ai_state["model"] = model_id
            return {"received": True}

        # Track AI input text
        if action == "textChanged" and target == "ai.input":
            ai_state["input_text"] = payload.get("text", "")
            return {"received": True}

        # Ctrl+Enter in ai.input sends the message
        if action == "keyDown" and target == "ai.input":
            keyval = payload.get("keyval", 0)
            modifiers = payload.get("modifiers", 0)
            if keyval in (0xff0d, 0x0000ff0d) and (modifiers & 4):  # Ctrl+Enter
                if not ai_state.get("generating") and ai_state.get("input_text", "").strip():
                    _deferred(_ai_send)
                return {"received": True}

        if action not in ("mouseMove", "mouseDown", "mouseUp"):
            print(json.dumps({"ui.event": params}, indent=2), flush=True)
        return {"received": True}

    initial_ide_state = _current_layout_state()
    builder = build_document()
    _init_editor_state()
    document_json = builder.to_json(indent=2)
    artifact_root = Path(__file__).resolve().parent / "artifacts"
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(document_json, encoding="utf-8")
    worker = None
    _first_connect = True
    try:
      while True:  # reconnect loop — continues only when _ui_server manages the process
        try:
          with ElaraUiRpcClient(args.host, args.port) as client:
            client_ref["client"] = client
            client.set_find_widget_artifact_root(str(artifact_root))
            if args.event_log:
                client.set_event_log(args.event_log)
            # Open per-session event log file.
            _artifacts_dir = Path(__file__).resolve().parent / "artifacts"
            _artifacts_dir.mkdir(exist_ok=True)
            _session_log_path = _artifacts_dir / f"event-{time.strftime('%Y%m%d-%H%M%S')}.jsonl"
            try:
                _event_log_fh[0] = open(_session_log_path, "a", encoding="utf-8")
            except Exception:
                pass
            _push_event("session_start", host=args.host, port=args.port,
                        log_file=str(_session_log_path))

            # Wrap client.call so every outgoing UI RPC call is logged.
            # This is the single most useful trace for reproducing UI crashes.
            _orig_client_call = client.call
            def _logged_client_call(method, params=None, timeout=5.0):
                _push_event("ui_rpc_out", method=method,
                            params=_trim_for_log(params) if isinstance(params, dict) else params)
                try:
                    return _orig_client_call(method, params, timeout)
                except Exception as _rpc_exc:
                    _push_event("ui_rpc_out_error", method=method, error=str(_rpc_exc))
                    raise
            client.call = _logged_client_call

            client.add_handler("ui.event", on_ui_event)
            load_result = client.load_document(builder)
            print(json.dumps(load_result, indent=2))
            if bool((initial_ide_state.get("window", {}) if isinstance(initial_ide_state, dict) else {}).get("maximized", False)):
                try:
                    client.set_window_maximized(True)
                except Exception:
                    pass
            try:
                client.call("ui.setVisible", {"target": "editor.tabs", "visible": False})
            except Exception:
                pass
            if not app_state.get("project_root"):
                try:
                    client.call("ui.setVisible", {"target": "app.toolbar", "visible": True})
                except Exception:
                    pass
                try:
                    _set_project_toolbar_enabled(client, False)
                except Exception:
                    pass
                for panel in _NAV_PANELS.values():
                    try:
                        client.call("ui.setVisible", {"target": panel, "visible": False})
                    except Exception:
                        pass
                try:
                    client.call("ui.setVisible", {"target": "nav.panel", "visible": True})
                    client.call("ui.setVisible", {"target": "nav.tree", "visible": False})
                    client.call("ui.setVisible", {"target": "nav.no_project", "visible": True})
                except Exception:
                    pass
            else:
                try:
                    _set_project_toolbar_enabled(client, True)
                except Exception:
                    pass
                _switch_nav_view(client, app_state.get("nav_view", "files"))
            for tab_id in editor_state:
                _refresh_e_tab(client, tab_id)
            if app_state.get("active_editor_tab"):
                _refresh_debug_sidebars(client, app_state["active_editor_tab"])
            _epa_dbg_set_vm_button(_epa_dbg_running())
            try:
                client.set_text("ai.history", _ai_format_history())
            except Exception:
                pass
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

            # Wire AI RPC callbacks now that all closures exist and client is live.
            # Only define and start once; across reconnects the callbacks stay valid
            # because they all reference client_ref (the shared dict), not client directly.
            if args.ai_rpc_port and _first_connect:
                def _ai_rpc_compile_tab(tab_id: str) -> dict:
                    state = editor_state.get(tab_id)
                    if not state:
                        return {"tab_id": tab_id, "asm": "", "error": "no_such_tab", "compiled_at": 0.0}
                    source_text = state.get("source_text", "")
                    tab_entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
                    source_dir = Path(tab_entry["path"]).parent if tab_entry and tab_entry.get("path") else None
                    try:
                        result = _compile_e_source(source_text, source_dir)
                    except Exception as exc:
                        return {"tab_id": tab_id, "asm": "", "error": str(exc), "compiled_at": 0.0}
                    ts = time.time()
                    state["epa_text"] = result["epa_text"]
                    state["epa_block_map"] = result.get("epa_block_map", {})
                    state["compile_error"] = result.get("message", "")
                    state["epa_compiled_at"] = ts
                    state["epa_error"] = result.get("message") if not result["ok"] else None
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            epa_out = result["epa_text"] if result["ok"] else ""
                            c.set_text(ids["epa"], epa_out)
                            c.set_code_editor_diagnostics(ids["source"], result.get("diagnostics", []))
                        except Exception:
                            pass
                    return {
                        "tab_id": tab_id,
                        "asm": result["epa_text"],
                        "error": result.get("message") if not result["ok"] else None,
                        "compiled_at": ts,
                    }

                def _ai_rpc_open_file(path: str):
                    c = client_ref.get("client")
                    if not c:
                        return None
                    # Check if already open
                    existing = next((t for t in tab_list if t.get("path") == path), None)
                    if existing:
                        return existing["tab_id"]
                    _open_file_tab(c, path, True)
                    found = next((t for t in tab_list if t.get("path") == path), None)
                    return found["tab_id"] if found else None

                def _ai_rpc_switch_view(tab_id: str, view: str):
                    state = editor_state.get(tab_id)
                    if state:
                        state["view"] = view
                    c = client_ref.get("client")
                    if c:
                        _deferred(lambda: _apply_editor_view(c, tab_id, set_focus=False))

                def _ai_rpc_set_editor_content(tab_id: str, content: str):
                    state = editor_state.get(tab_id)
                    if state:
                        state["source_text"] = content
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            c.set_text(ids["source"], content)
                        except Exception:
                            pass

                def _ai_rpc_set_active_tab(tab_id: str):
                    t = next((x for x in tab_list if x.get("tab_id") == tab_id), None)
                    if not t:
                        return
                    c = client_ref.get("client")
                    if c:
                        try:
                            c.call("ui.setActiveTab", {"target": "editor.tabs", "index": t["index"]})
                        except Exception:
                            pass
                    app_state["active_editor_tab"] = tab_id

                def _ai_rpc_close_tab(tab_id: str):
                    t = next((x for x in tab_list if x.get("tab_id") == tab_id), None)
                    if not t:
                        return
                    c = client_ref.get("client")
                    if c:
                        try:
                            c.call("ui.removeTab", {"target": "editor.tabs", "index": t["index"]})
                        except Exception:
                            pass
                    tab_list[:] = [x for x in tab_list if x.get("tab_id") != tab_id]
                    editor_state.pop(tab_id, None)

                def _ai_rpc_get_nav_tree() -> list:
                    return app_state.get("nav_tree_nodes", [])

                def _ai_rpc_set_node_expanded(node_id: str, expanded: bool) -> bool:
                    nodes = app_state.get("nav_tree_nodes")
                    if not nodes:
                        return False

                    def _toggle(node_list):
                        for node in node_list:
                            if node.get("id") == node_id:
                                node["expanded"] = expanded
                                return True
                            if _toggle(node.get("children", [])):
                                return True
                        return False

                    changed = _toggle(nodes)
                    if changed:
                        c = client_ref.get("client")
                        if c:
                            try:
                                document = json.dumps({"nodes": nodes}, separators=(",", ":"))
                                c.call("ui.replaceChildren", {"target": "nav.tree", "document": document})
                            except Exception:
                                pass
                    return changed

                def _ai_rpc_tree_open_file(path: str) -> str:
                    c = client_ref.get("client")
                    if not c:
                        return ""
                    _open_file_tab(c, path, True)
                    found = next((t for t in tab_list if t.get("path") == path), None)
                    return found["tab_id"] if found else ""

                def _ai_rpc_editor_replace_range(
                    tab_id: str,
                    from_line: int, from_col: int,
                    to_line: int, to_col: int,
                    replacement: str,
                ) -> dict:
                    state = editor_state.get(tab_id)
                    if not state:
                        raise KeyError(f"no_such_tab: {tab_id}")
                    text = state.get("source_text", "")
                    lines = text.split("\n")

                    def _to_offset(line_1, col_0):
                        line_1 = max(1, min(line_1, len(lines)))
                        col_0 = max(0, min(col_0, len(lines[line_1 - 1])))
                        return sum(len(lines[i]) + 1 for i in range(line_1 - 1)) + col_0

                    from_off = _to_offset(from_line, from_col)
                    to_off = _to_offset(to_line, to_col)
                    if from_off > to_off:
                        from_off, to_off = to_off, from_off
                    new_text = text[:from_off] + replacement + text[to_off:]
                    state["source_text"] = new_text
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            c.set_text(ids["source"], new_text)
                        except Exception:
                            pass
                    new_lines = new_text.split("\n")
                    ins_chars = len(replacement)
                    ins_end_off = from_off + ins_chars
                    ins_end_line = new_text[:ins_end_off].count("\n") + 1
                    ins_end_col = ins_end_off - new_text[:ins_end_off].rfind("\n") - 1
                    return {
                        "tab_id": tab_id,
                        "lines_total": len(new_lines),
                        "chars_total": len(new_text),
                        "cursor_line": ins_end_line,
                        "cursor_col": ins_end_col,
                    }

                def _ai_rpc_ui_call(method: str, params: dict):
                    c = client_ref.get("client")
                    if not c:
                        raise RuntimeError("ui_unavailable: UI client not connected")
                    return c.call(f"ui.{method}", params)

                def _ai_rpc_open_project(path: str) -> dict:
                    c = client_ref.get("client")
                    if not c:
                        raise RuntimeError("ui_unavailable: UI client not connected")
                    _open_project(c, path)
                    return {
                        "project_root": app_state.get("project_root", ""),
                        "project_name": app_state.get("project_name", ""),
                    }

                def _ai_rpc_get_exceptions(limit: int = 50) -> list:
                    with _exception_log_lock:
                        entries = list(_exception_log)
                    return entries[-limit:] if limit > 0 else entries

                def _ai_rpc_clear_exceptions() -> dict:
                    with _exception_log_lock:
                        count = len(_exception_log)
                        _exception_log.clear()
                    return {"cleared": count}

                def _ai_rpc_get_ui_status() -> dict:
                    c = client_ref.get("client")
                    proc = _ui_server.get("proc")
                    pid = proc.pid if proc else None
                    alive = proc is not None and proc.poll() is None
                    recent_output = list(_ui_server.get("output_lines", []))[-50:]
                    return {
                        "connected": bool(c and c._running),
                        "managed_process": proc is not None,
                        "process_alive": alive,
                        "process_pid": pid,
                        "server_cmd": _ui_server.get("cmd", []),
                        "recent_output": recent_output,
                    }

                def _ai_rpc_restart_ui(use_gdb: bool = False) -> dict:
                    proc = _ui_server.get("proc")
                    if proc and proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=4)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()

                    cmd = _resolve_ui_server_cmd()
                    _ui_server["output_lines"] = []
                    if use_gdb:
                        cmd = [
                            "gdb", "--batch",
                            "-ex", "handle SIGPIPE nostop noprint",
                            "-ex", "run",
                            "-ex", "thread apply all bt full",
                            "--args",
                        ] + cmd

                    out_lines = _ui_server["output_lines"]

                    def _reader(stream, tag):
                        for raw in stream:
                            line = raw.decode(errors="replace").rstrip()
                            out_lines.append(f"[{tag}] {line}")
                            if len(out_lines) > 500:
                                del out_lines[:len(out_lines) - 500]

                    new_proc = subprocess.Popen(
                        cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                    )
                    threading.Thread(target=_reader, args=(new_proc.stdout, "stdout"), daemon=True).start()
                    threading.Thread(target=_reader, args=(new_proc.stderr, "stderr"), daemon=True).start()
                    _ui_server["proc"] = new_proc
                    _ui_server["cmd"] = cmd
                    app_state["_ui_reconnect_requested"] = True
                    return {"pid": new_proc.pid, "cmd": cmd, "use_gdb": use_gdb}

                def _ai_rpc_get_event_log(limit: int = 100, type_filter: str = "") -> list:
                    with _event_log_lock:
                        entries = list(_event_log)
                    if type_filter:
                        entries = [e for e in entries if e.get("type") == type_filter]
                    if limit and len(entries) > limit:
                        entries = entries[-limit:]
                    return entries

                def _ai_rpc_clear_event_log() -> dict:
                    with _event_log_lock:
                        n = len(_event_log)
                        _event_log.clear()
                    return {"cleared": n}

                def _ai_rpc_list_ingress_types() -> dict:
                    return _parse_type_defs()

                def _ai_rpc_list_ingress_profiles(type_name: str) -> list:
                    items = _profiles_for_type(type_name)
                    d = _profiles_dir(type_name)
                    return [
                        {"name": it["id"], "path": str(d / f"{it['id']}.json") if d else ""}
                        for it in items
                    ]

                def _ai_rpc_get_ingress_profile(type_name: str, profile_name: str) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open to read profiles")
                    path = d / f"{profile_name}.json"
                    if not path.is_file():
                        raise RuntimeError(f"io_error: profile not found: {profile_name}")
                    try:
                        data = json.loads(path.read_text(encoding="utf-8"))
                    except Exception as exc:
                        raise RuntimeError(f"io_error: {exc}")
                    return {
                        "type_name": type_name,
                        "profile_name": profile_name,
                        "fields": data.get("fields", {}),
                        "path": str(path),
                    }

                def _ai_rpc_save_ingress_profile(type_name: str, profile_name: str,
                                                  fields: dict) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open to save profiles")
                    d.mkdir(parents=True, exist_ok=True)
                    out = {"type": type_name, "name": profile_name, "fields": fields}
                    path = d / f"{profile_name}.json"
                    path.write_text(json.dumps(out, indent=2), encoding="utf-8")
                    # Refresh the UI list if the currently selected type matches
                    ui_c = client_ref.get("client")
                    if ui_c:
                        cur_type = app_state.get("debug_ingress_type", "")
                        if not cur_type or cur_type == type_name:
                            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(ui_c, tn))
                    return {
                        "type_name": type_name,
                        "profile_name": profile_name,
                        "path": str(path),
                        "fields_written": len(fields),
                    }

                def _ai_rpc_delete_ingress_profile(type_name: str, profile_name: str) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open")
                    path = d / f"{profile_name}.json"
                    deleted = path.is_file()
                    if deleted:
                        try:
                            path.unlink()
                        except OSError as exc:
                            raise RuntimeError(f"io_error: {exc}")
                    ui_c = client_ref.get("client")
                    if ui_c and deleted:
                        cur_type = app_state.get("debug_ingress_type", "")
                        if not cur_type or cur_type == type_name:
                            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(ui_c, tn))
                    return {"deleted": deleted, "path": str(path)}

                ide_bindings._compile_tab = _ai_rpc_compile_tab
                ide_bindings._open_file = _ai_rpc_open_file
                ide_bindings._switch_view = _ai_rpc_switch_view
                ide_bindings._set_editor_content = _ai_rpc_set_editor_content
                ide_bindings._set_active_tab = _ai_rpc_set_active_tab
                ide_bindings._close_tab = _ai_rpc_close_tab
                ide_bindings._get_nav_tree = _ai_rpc_get_nav_tree
                ide_bindings._set_node_expanded = _ai_rpc_set_node_expanded
                ide_bindings._tree_open_file = _ai_rpc_tree_open_file
                ide_bindings._editor_replace_range = _ai_rpc_editor_replace_range
                ide_bindings._ui_call = _ai_rpc_ui_call
                ide_bindings._open_project = _ai_rpc_open_project
                ide_bindings._get_exceptions = _ai_rpc_get_exceptions
                ide_bindings._clear_exceptions = _ai_rpc_clear_exceptions
                ide_bindings._get_ui_status = _ai_rpc_get_ui_status
                ide_bindings._restart_ui = _ai_rpc_restart_ui
                ide_bindings._get_event_log = _ai_rpc_get_event_log
                ide_bindings._clear_event_log = _ai_rpc_clear_event_log
                ide_bindings._log_event = _push_event
                ide_bindings._list_ingress_types = _ai_rpc_list_ingress_types
                ide_bindings._list_ingress_profiles = _ai_rpc_list_ingress_profiles
                ide_bindings._get_ingress_profile = _ai_rpc_get_ingress_profile
                ide_bindings._save_ingress_profile = _ai_rpc_save_ingress_profile
                ide_bindings._delete_ingress_profile = _ai_rpc_delete_ingress_profile

                ai_rpc_server = AiRpcServer(port=args.ai_rpc_port, ide=ide_bindings)
                ai_rpc_server.start()
                _first_connect = False

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
            next_layout_persist = 0.0
            while client._running and not app_state.get("_ui_reconnect_requested"):
                now = time.monotonic()
                if now >= next_layout_persist:
                    _persist_runtime_layout_state(client)
                    next_layout_persist = now + 1.0
                time.sleep(0.25)

            if not client._running and not app_state.get("_ui_reconnect_requested"):
                _push_event("ui_disconnect", reason="unexpected",
                            note="See recent ui_rpc_out entries to reproduce crash under gdb")
                _push_exception(Exception("UI RPC connection dropped unexpectedly"), "main_loop")
            else:
                _push_event("ui_disconnect", reason="requested")

        except (OSError, ElaraUiRpcError) as _conn_exc:
            _push_exception(_conn_exc, "ui_connect")
            if _ui_server.get("proc") is None:
                raise SystemExit(str(_conn_exc))

        finally:
            # Close the session log file; the next connect iteration opens a new one.
            fh = _event_log_fh[0]
            if fh is not None:
                try:
                    fh.close()
                except Exception:
                    pass
                _event_log_fh[0] = None

        # --- Decide whether to reconnect ---
        client_ref["client"] = None
        _reconnect = app_state.pop("_ui_reconnect_requested", False)
        if not _reconnect and _ui_server.get("proc") is not None:
            # UI dropped; if we're managing the process, stay alive and wait
            proc = _ui_server["proc"]
            if proc.poll() is None:
                _reconnect = True  # managed proc still running (we asked it to restart)

        if not _reconnect:
            break

        # Wait up to 10 s for the new server to start accepting connections.
        print(json.dumps({"ui_reconnecting": True}), flush=True)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            try:
                import socket as _sock
                _s = _sock.create_connection((args.host, args.port), timeout=0.5)
                _s.close()
                break
            except OSError:
                time.sleep(0.5)

    except KeyboardInterrupt:
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
        return


    finally:
        try:
            if "client_ref" in locals():
                client = client_ref.get("client")
                if client is not None:
                    _persist_runtime_layout_state(client)
        except Exception:
            pass
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
        if ai_rpc_server is not None:
            try:
                ai_rpc_server.stop()
            except Exception:
                pass
        try:
            _epa_dbg_stop()
        except Exception:
            pass


if __name__ == "__main__":
    main()

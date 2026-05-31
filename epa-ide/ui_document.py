from elara_ui.builder import UiDocumentBuilder

from editor_tabs import (
    _editor_ids, _project_toolbar_items, _build_kernel_row_widgets,
    _create_e_tab, _create_python_tab, _create_cpp_tab
)
from constants import INITIAL_E_TABS, AI_MODELS


def build_document():
    import app as _app_module
    _current_layout_state = _app_module._current_layout_state
    _use_system_window_header = _app_module._use_system_window_header
    _right_panel_visible = _app_module._right_panel_visible
    _bottom_panel_visible = _app_module._bottom_panel_visible
    _layout_value = _app_module._layout_value
    _window_value = _app_module._window_value
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
    ui.add_menu_bar_button("app.menu", "bottom_panel_toggle", "▤", "app.toggle_bottom_panel")
    ui.add_menu_bar_button("app.menu", "right_panel_toggle", "◨", "app.toggle_right_panel")
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
            {"id": "build.compile_epa", "label": "Compile &E/EPA", "shortcut": "Ctrl+Shift+B"},
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
    _NAV_TECH_TABS = [
        ("epa",    "E / EPA", "nav.tree.epa",    "nav.tech_panel.epa",    "nav.add_tech_wrap.epa",    "nav.add_tech_btn.epa",    "new_file.E",      "Add EPA Technology",    "nav.add_tech.epa"),
        ("cpp",    "C++",     "nav.tree.cpp",    "nav.tech_panel.cpp",    "nav.add_tech_wrap.cpp",    "nav.add_tech_btn.cpp",    "new_file.Cpp",    "Add C++ Technology",    "nav.add_tech.cpp"),
        ("python", "Python",  "nav.tree.python", "nav.tech_panel.python", "nav.add_tech_wrap.python", "nav.add_tech_btn.python", "new_file.Python", "Add Python Technology", "nav.add_tech.python"),
    ]
    for (_tech, _tab_title, _tree_id, _panel_id, _wrap_id, _btn_id,
         _new_file_action, _add_tech_label, _add_tech_action) in _NAV_TECH_TABS:
        ui.create_grid(_panel_id)
        ui.add_grid_column_fill(_panel_id)
        ui.add_grid_row_fill(_panel_id)
        ui.create_tree_view(_tree_id)
        ui.set_property_number(_tree_id, "font_size", 14)
        ui.set_section_json(_tree_id, "nodes", [])
        ui.create_grid(_wrap_id)
        ui.add_grid_column_weighted_fill(_wrap_id, 1)
        ui.add_grid_column_exact(_wrap_id, 160)
        ui.add_grid_column_weighted_fill(_wrap_id, 1)
        ui.add_grid_row_weighted_fill(_wrap_id, 1)
        ui.add_grid_row_exact(_wrap_id, 36)
        ui.add_grid_row_weighted_fill(_wrap_id, 1)
        ui.create_button(_btn_id, _add_tech_label, _add_tech_action)
        ui.place_grid_child(_wrap_id, _btn_id, 1, 1)
        ui.place_grid_child(_panel_id, _tree_id, 0, 0)
        ui.place_grid_child(_panel_id, _wrap_id, 0, 0)
    ui.create_tabs("nav.file_tabs")
    for (_tech, _tab_title, _tree_id, _panel_id, _wrap_id, _btn_id,
         _new_file_action, _add_tech_label, _add_tech_action) in _NAV_TECH_TABS:
        ui.add_tab("nav.file_tabs", _tab_title, _panel_id,
                   button_glyph="+", button_action=_new_file_action)
    ui.set_property_bool("nav.file_tabs", "visible", False)

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

    ui.create_grid("nav.title_bar")
    ui.add_grid_column_exact("nav.title_bar", 8)
    ui.add_grid_column_fill("nav.title_bar")
    ui.add_grid_column_exact("nav.title_bar", 28)
    ui.add_grid_column_exact("nav.title_bar", 4)
    ui.add_grid_column_exact("nav.title_bar", 28)
    ui.add_grid_row_fill("nav.title_bar")
    ui.create_label("nav.project_title", "No Project", 11)
    ui.create_button("nav.filter_toggle", "≡", "nav.filter_toggle")
    ui.create_button("nav.refresh", "↺", "nav.refresh")
    ui.place_grid_child("nav.title_bar", "nav.project_title", 1, 0)
    ui.place_grid_child("nav.title_bar", "nav.filter_toggle", 2, 0)
    ui.place_grid_child("nav.title_bar", "nav.refresh", 4, 0)

    ui.place_grid_child("nav.panel", "nav.title_bar", 0, 0)
    ui.place_grid_child("nav.panel", "nav.no_project", 0, 1)
    ui.place_grid_child("nav.panel", "nav.file_tabs", 0, 1)

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
    ui.add_grid_row_fill("nav.debug_panel")              # 1  language debug tabs

    ui.create_grid("nav.debug_header")
    ui.add_grid_column_exact("nav.debug_header", 8)
    ui.add_grid_column_fill("nav.debug_header")
    ui.add_grid_row_fill("nav.debug_header")
    ui.create_label("nav.debug_title", "DEBUG", 11)
    ui.place_grid_child("nav.debug_header", "nav.debug_title", 1, 0)

    ui.create_tabs("nav.debug.lang_tabs")

    ui.create_grid("nav.debug.epa_panel")
    ui.add_grid_column_fill("nav.debug.epa_panel")
    ui.add_grid_row_exact("nav.debug.epa_panel", 46)         # 0 vm controls (top)
    ui.add_grid_row_exact("nav.debug.epa_panel", 246)        # 1 ingress designer
    ui.add_grid_row_exact("nav.debug.epa_panel", 22)         # 2 kernels label
    ui.add_grid_row_weighted_fill("nav.debug.epa_panel", 1)  # 3 kernel list

    ui.create_grid("nav.debug.cpp_panel")
    ui.add_grid_column_fill("nav.debug.cpp_panel")
    ui.add_grid_row_exact("nav.debug.cpp_panel", 204)        # 0 controls
    ui.add_grid_row_exact("nav.debug.cpp_panel", 22)         # 1 threads label
    ui.add_grid_row_weighted_fill("nav.debug.cpp_panel", 1)  # 2 threads list

    ui.create_grid("nav.debug.cpp_controls")
    ui.add_grid_column_exact("nav.debug.cpp_controls", 8)
    ui.add_grid_column_fill("nav.debug.cpp_controls")
    ui.add_grid_column_exact("nav.debug.cpp_controls", 8)
    ui.add_grid_row_exact("nav.debug.cpp_controls", 22)   # 0 title
    ui.add_grid_row_exact("nav.debug.cpp_controls", 18)   # 1 status
    ui.add_grid_row_exact("nav.debug.cpp_controls", 30)   # 2 start/stop buttons
    ui.add_grid_row_exact("nav.debug.cpp_controls", 8)    # 3 gap
    ui.add_grid_row_exact("nav.debug.cpp_controls", 20)   # 4 step label
    ui.add_grid_row_exact("nav.debug.cpp_controls", 30)   # 5 step row
    ui.add_grid_row_exact("nav.debug.cpp_controls", 8)    # 6 gap
    ui.add_grid_row_exact("nav.debug.cpp_controls", 30)   # 7 lower row
    ui.add_grid_row_exact("nav.debug.cpp_controls", 30)   # 8 status text
    ui.add_grid_row_exact("nav.debug.cpp_controls", 8)    # 9 bottom gap

    ui.create_label("nav.debug.cpp_title", "C++ DEBUG CONTROL", 10)
    ui.set_property_bool("nav.debug.cpp_title", "enabled", False)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_title", 1, 0)

    ui.create_label("nav.debug.cpp_vm_status", "●  C++ debugger idle", 10)
    ui.set_property_string("nav.debug.cpp_vm_status", "foreground_color", "#777777")
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_vm_status", 1, 1)

    ui.create_grid("nav.debug.cpp_start_stop_row")
    ui.add_grid_column_fill("nav.debug.cpp_start_stop_row")
    ui.add_grid_column_exact("nav.debug.cpp_start_stop_row", 4)
    ui.add_grid_column_fill("nav.debug.cpp_start_stop_row")
    ui.add_grid_row_fill("nav.debug.cpp_start_stop_row")
    ui.create_button("nav.debug.cpp_reset", "▶  Start", "debug.cpp.reset")
    ui.set_property_number("nav.debug.cpp_reset", "font_size", 11)
    ui.create_button("nav.debug.cpp_stop", "■  Stop", "debug.cpp.stop")
    ui.set_property_number("nav.debug.cpp_stop", "font_size", 11)
    ui.set_property_bool("nav.debug.cpp_stop", "enabled", False)
    ui.place_grid_child("nav.debug.cpp_start_stop_row", "nav.debug.cpp_reset", 0, 0)
    ui.place_grid_child("nav.debug.cpp_start_stop_row", "nav.debug.cpp_stop", 2, 0)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_start_stop_row", 1, 2)

    ui.create_label("nav.debug.cpp_step_label", "STEP CONTROLS", 10)
    ui.set_property_bool("nav.debug.cpp_step_label", "enabled", False)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_step_label", 1, 4)

    ui.create_grid("nav.debug.cpp_step_row")
    ui.add_grid_column_fill("nav.debug.cpp_step_row")
    ui.add_grid_column_exact("nav.debug.cpp_step_row", 4)
    ui.add_grid_column_fill("nav.debug.cpp_step_row")
    ui.add_grid_column_exact("nav.debug.cpp_step_row", 4)
    ui.add_grid_column_fill("nav.debug.cpp_step_row")
    ui.add_grid_row_fill("nav.debug.cpp_step_row")
    ui.create_button("nav.debug.cpp_continue", "▶ Continue", "debug.cpp.continue")
    ui.create_button("nav.debug.cpp_step_over", "↷ Step Over", "debug.cpp.step_over")
    ui.create_button("nav.debug.cpp_step_into", "↓ Step Into", "debug.cpp.step_into")
    ui.set_property_number("nav.debug.cpp_continue", "font_size", 11)
    ui.set_property_number("nav.debug.cpp_step_over", "font_size", 11)
    ui.set_property_number("nav.debug.cpp_step_into", "font_size", 11)
    ui.place_grid_child("nav.debug.cpp_step_row", "nav.debug.cpp_continue", 0, 0)
    ui.place_grid_child("nav.debug.cpp_step_row", "nav.debug.cpp_step_over", 2, 0)
    ui.place_grid_child("nav.debug.cpp_step_row", "nav.debug.cpp_step_into", 4, 0)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_step_row", 1, 5)

    ui.create_grid("nav.debug.cpp_lower_row")
    ui.add_grid_column_fill("nav.debug.cpp_lower_row")
    ui.add_grid_column_exact("nav.debug.cpp_lower_row", 4)
    ui.add_grid_column_fill("nav.debug.cpp_lower_row")
    ui.add_grid_row_fill("nav.debug.cpp_lower_row")
    ui.create_button("nav.debug.cpp_step_out", "↑ Step Out", "debug.cpp.step_out")
    ui.create_button("nav.debug.cpp_pause", "⏸ Pause", "debug.cpp.pause")
    ui.set_property_number("nav.debug.cpp_step_out", "font_size", 11)
    ui.set_property_number("nav.debug.cpp_pause", "font_size", 11)
    ui.place_grid_child("nav.debug.cpp_lower_row", "nav.debug.cpp_step_out", 0, 0)
    ui.place_grid_child("nav.debug.cpp_lower_row", "nav.debug.cpp_pause", 2, 0)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_lower_row", 1, 7)

    ui.create_label(
        "nav.debug.cpp_status",
        "GDB bridge not linked yet. These controls are reserved for host-side stepping.",
        11,
    )
    ui.set_property_bool("nav.debug.cpp_status", "enabled", False)
    ui.place_grid_child("nav.debug.cpp_controls", "nav.debug.cpp_status", 1, 8)

    ui.create_label("nav.debug.cpp_threads_label", "CURRENT THREADS", 10)
    ui.set_property_bool("nav.debug.cpp_threads_label", "enabled", False)
    ui.create_list_view("nav.debug.cpp_threads")
    ui.set_property_number("nav.debug.cpp_threads", "font_size", 12)
    ui.set_section_json("nav.debug.cpp_threads", "items", [
        {"id": "thread.placeholder.0", "label": "No GDB session attached"},
    ])

    ui.place_grid_child("nav.debug.cpp_panel", "nav.debug.cpp_controls", 0, 0)
    ui.place_grid_child("nav.debug.cpp_panel", "nav.debug.cpp_threads_label", 0, 1)
    ui.place_grid_child("nav.debug.cpp_panel", "nav.debug.cpp_threads", 0, 2)

    ui.create_grid("nav.debug.python_panel")
    ui.add_grid_column_fill("nav.debug.python_panel")
    ui.add_grid_row_exact("nav.debug.python_panel", 188)        # 0 controls
    ui.add_grid_row_exact("nav.debug.python_panel", 22)         # 1 threads label
    ui.add_grid_row_weighted_fill("nav.debug.python_panel", 1)  # 2 threads list

    ui.create_grid("nav.debug.python_controls")
    ui.add_grid_column_exact("nav.debug.python_controls", 8)
    ui.add_grid_column_fill("nav.debug.python_controls")
    ui.add_grid_column_exact("nav.debug.python_controls", 4)
    ui.add_grid_column_exact("nav.debug.python_controls", 80)
    ui.add_grid_column_exact("nav.debug.python_controls", 8)
    ui.add_grid_column_exact("nav.debug.python_controls", 80)
    ui.add_grid_column_exact("nav.debug.python_controls", 8)
    ui.add_grid_row_exact("nav.debug.python_controls", 22)   # 0 title
    ui.add_grid_row_exact("nav.debug.python_controls", 24)   # 1 status + start/stop
    ui.add_grid_row_exact("nav.debug.python_controls", 20)   # 2 section label
    ui.add_grid_row_exact("nav.debug.python_controls", 30)   # 3 step row
    ui.add_grid_row_exact("nav.debug.python_controls", 12)   # 4 gap
    ui.add_grid_row_exact("nav.debug.python_controls", 30)   # 5 lower row
    ui.add_grid_row_exact("nav.debug.python_controls", 30)   # 6 status text
    ui.add_grid_row_exact("nav.debug.python_controls", 12)   # 7 bottom gap

    ui.create_label("nav.debug.python_title", "PYTHON DEBUG CONTROL", 10)
    ui.set_property_bool("nav.debug.python_title", "enabled", False)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_title", 1, 0, 5, 1)

    ui.create_label("nav.debug.python_vm_status", "●  Python debugger idle", 10)
    ui.set_property_string("nav.debug.python_vm_status", "foreground_color", "#777777")
    ui.create_button("nav.debug.python_reset", "▶  Start", "debug.python.reset")
    ui.set_property_number("nav.debug.python_reset", "font_size", 11)
    ui.create_button("nav.debug.python_stop", "■  Stop", "debug.python.stop")
    ui.set_property_number("nav.debug.python_stop", "font_size", 11)
    ui.set_property_bool("nav.debug.python_stop", "enabled", False)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_vm_status", 1, 1)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_reset", 3, 1)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_stop", 5, 1)

    ui.create_label("nav.debug.python_step_label", "STEP CONTROLS", 10)
    ui.set_property_bool("nav.debug.python_step_label", "enabled", False)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_step_label", 1, 2, 5, 1)

    ui.create_grid("nav.debug.python_step_row")
    ui.add_grid_column_fill("nav.debug.python_step_row")
    ui.add_grid_column_exact("nav.debug.python_step_row", 4)
    ui.add_grid_column_fill("nav.debug.python_step_row")
    ui.add_grid_column_exact("nav.debug.python_step_row", 4)
    ui.add_grid_column_fill("nav.debug.python_step_row")
    ui.add_grid_row_fill("nav.debug.python_step_row")
    ui.create_button("nav.debug.python_continue", "▶ Continue", "debug.python.continue")
    ui.create_button("nav.debug.python_step_over", "↷ Step Over", "debug.python.step_over")
    ui.create_button("nav.debug.python_step_into", "↓ Step Into", "debug.python.step_into")
    ui.set_property_number("nav.debug.python_continue", "font_size", 11)
    ui.set_property_number("nav.debug.python_step_over", "font_size", 11)
    ui.set_property_number("nav.debug.python_step_into", "font_size", 11)
    ui.place_grid_child("nav.debug.python_step_row", "nav.debug.python_continue", 0, 0)
    ui.place_grid_child("nav.debug.python_step_row", "nav.debug.python_step_over", 2, 0)
    ui.place_grid_child("nav.debug.python_step_row", "nav.debug.python_step_into", 4, 0)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_step_row", 1, 3, 5, 1)

    ui.create_grid("nav.debug.python_lower_row")
    ui.add_grid_column_fill("nav.debug.python_lower_row")
    ui.add_grid_column_exact("nav.debug.python_lower_row", 4)
    ui.add_grid_column_fill("nav.debug.python_lower_row")
    ui.add_grid_row_fill("nav.debug.python_lower_row")
    ui.create_button("nav.debug.python_step_out", "↑ Step Out", "debug.python.step_out")
    ui.create_button("nav.debug.python_pause", "⏸ Pause", "debug.python.pause")
    ui.set_property_number("nav.debug.python_step_out", "font_size", 11)
    ui.set_property_number("nav.debug.python_pause", "font_size", 11)
    ui.place_grid_child("nav.debug.python_lower_row", "nav.debug.python_step_out", 0, 0)
    ui.place_grid_child("nav.debug.python_lower_row", "nav.debug.python_pause", 2, 0)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_lower_row", 1, 5, 5, 1)

    ui.create_label(
        "nav.debug.python_status",
        "Python debug bridge not linked yet. These controls are reserved for interpreted host-side stepping.",
        11,
    )
    ui.set_property_bool("nav.debug.python_status", "enabled", False)
    ui.place_grid_child("nav.debug.python_controls", "nav.debug.python_status", 1, 6, 5, 1)

    ui.create_label("nav.debug.python_threads_label", "CURRENT THREADS", 10)
    ui.set_property_bool("nav.debug.python_threads_label", "enabled", False)
    ui.create_list_view("nav.debug.python_threads")
    ui.set_property_number("nav.debug.python_threads", "font_size", 12)
    ui.set_section_json("nav.debug.python_threads", "items", [
        {"id": "python.thread.placeholder.0", "label": "No Python debug session attached"},
    ])

    ui.place_grid_child("nav.debug.python_panel", "nav.debug.python_controls", 0, 0)
    ui.place_grid_child("nav.debug.python_panel", "nav.debug.python_threads_label", 0, 1)
    ui.place_grid_child("nav.debug.python_panel", "nav.debug.python_threads", 0, 2)

    # VM control strip (top of EPA panel, consistent with C++/Python layout)
    ui.create_grid("nav.debug.vm_controls")
    ui.add_grid_column_exact("nav.debug.vm_controls", 8)
    ui.add_grid_column_fill("nav.debug.vm_controls")
    ui.add_grid_column_exact("nav.debug.vm_controls", 4)
    ui.add_grid_column_exact("nav.debug.vm_controls", 80)
    ui.add_grid_column_exact("nav.debug.vm_controls", 8)
    ui.add_grid_column_exact("nav.debug.vm_controls", 80)
    ui.add_grid_column_exact("nav.debug.vm_controls", 8)
    ui.add_grid_row_exact("nav.debug.vm_controls", 22)
    ui.add_grid_row_exact("nav.debug.vm_controls", 24)
    ui.create_label("nav.debug.vm_title", "EPA DEBUG CONTROL", 10)
    ui.set_property_bool("nav.debug.vm_title", "enabled", False)
    ui.create_label("nav.debug.vm_status", "●  VM idle", 10)
    ui.set_property_string("nav.debug.vm_status", "foreground_color", "#777777")
    ui.create_button("nav.debug.vm_reset", "▶  Start", "debug.vm.reset")
    ui.set_property_number("nav.debug.vm_reset", "font_size", 11)
    ui.create_button("nav.debug.vm_stop", "■  Stop", "debug.vm.stop")
    ui.set_property_number("nav.debug.vm_stop", "font_size", 11)
    ui.set_property_bool("nav.debug.vm_stop", "enabled", False)
    ui.place_grid_child("nav.debug.vm_controls", "nav.debug.vm_title",  1, 0, 5, 1)
    ui.place_grid_child("nav.debug.vm_controls", "nav.debug.vm_status", 1, 1)
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

    ui.place_grid_child("nav.debug.epa_panel", "nav.debug.vm_controls",    0, 0)
    ui.place_grid_child("nav.debug.epa_panel", "nav.debug.ingress",        0, 1)
    ui.place_grid_child("nav.debug.epa_panel", "nav.debug.kernels_header", 0, 2)
    ui.place_grid_child("nav.debug.epa_panel", "nav.debug.kernels",        0, 3)
    ui.add_tab("nav.debug.lang_tabs", "EPA",    "nav.debug.epa_panel")
    ui.add_tab("nav.debug.lang_tabs", "C++",    "nav.debug.cpp_panel")
    ui.add_tab("nav.debug.lang_tabs", "Python", "nav.debug.python_panel")
    ui.place_grid_child("nav.debug_panel", "nav.debug_header",         0, 0)
    ui.place_grid_child("nav.debug_panel", "nav.debug.lang_tabs",      0, 1)
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
    ui.add_toolbar_item("bottom.toolbar", "bottom.host_io", "Host IO")
    ui.add_toolbar_item("bottom.toolbar", "bottom.console", "Console")
    ui.add_toolbar_item("bottom.toolbar", "bottom.terminal", "Terminal")
    ui.add_toolbar_item("bottom.toolbar", "bottom.status", "IDE Status")
    ui.add_toolbar_separator("bottom.toolbar")
    ui.add_toolbar_item("bottom.toolbar", "bottom.clear", "Clear")

    ui.create_rich_text_edit(
        "bottom.build_output",
        "Build output will appear here.",
    )
    ui.set_property_number("bottom.build_output", "font_size", 12)
    ui.set_property_bool("bottom.build_output", "read_only", True)
    ui.create_rich_text_edit(
        "bottom.host_io_output",
        "Host VM I/O will appear here.",
    )
    ui.set_property_number("bottom.host_io_output", "font_size", 12)
    ui.set_property_bool("bottom.host_io_output", "read_only", True)

    ui.create_grid("bottom.terminal_panel")
    ui.add_grid_column_fill("bottom.terminal_panel")
    ui.add_grid_row_fill("bottom.terminal_panel")
    ui.create_terminal("bottom.terminal_widget")
    ui.place_grid_child("bottom.terminal_panel", "bottom.terminal_widget", 0, 0)

    # Console panel (AI RPC text interface)
    ui.create_grid("bottom.console_panel")
    ui.add_grid_column_fill("bottom.console_panel")
    ui.add_grid_row_fill("bottom.console_panel")
    ui.add_grid_row_exact("bottom.console_panel", 28)
    ui.create_rich_text_edit(
        "bottom.console_output",
        "EPA-IDE Console — type 'help' for available commands.\n",
    )
    ui.set_property_number("bottom.console_output", "font_size", 12)
    ui.set_property_bool("bottom.console_output", "read_only", True)
    ui.create_text_input("bottom.console_input", "")
    ui.set_property_string("bottom.console_input", "placeholder", "method [params]")
    ui.set_property_number("bottom.console_input", "font_size", 12)
    ui.place_grid_child("bottom.console_panel", "bottom.console_output", 0, 0)
    ui.place_grid_child("bottom.console_panel", "bottom.console_input", 0, 1)

    # IDE Status panel (connection status display)
    ui.create_grid("bottom.status_panel")
    ui.add_grid_column_weighted_fill("bottom.status_panel", 1)
    ui.add_grid_column_weighted_fill("bottom.status_panel", 1)
    ui.add_grid_column_weighted_fill("bottom.status_panel", 1)
    ui.add_grid_column_weighted_fill("bottom.status_panel", 1)
    ui.add_grid_row_fill("bottom.status_panel")

    for _col_idx, (_sname, _stitle) in enumerate([
        ("epa",    "EPA DBG"),
        ("host",   "HOST INTERCONNECT"),
        ("cpp",    "C++ GDB"),
        ("python", "PYTHON LOGIC"),
    ]):
        _sid = f"bottom.status.{_sname}_section"
        ui.create_grid(_sid)
        ui.add_grid_column_exact(_sid, 12)   # col 0: left pad
        ui.add_grid_column_exact(_sid, 16)   # col 1: dot
        ui.add_grid_column_exact(_sid, 6)    # col 2: gap
        ui.add_grid_column_fill(_sid)        # col 3: label text
        ui.add_grid_column_exact(_sid, 4)    # col 4: inner gap
        ui.add_grid_column_exact(_sid, 56)   # col 5: ping dot or kill button
        ui.add_grid_column_exact(_sid, 8)    # col 6: right margin
        ui.add_grid_row_exact(_sid, 26)      # row 0: section title
        ui.add_grid_row_exact(_sid, 20)      # row 1: port
        ui.add_grid_row_exact(_sid, 22)      # row 2: primary status
        ui.add_grid_row_exact(_sid, 22)      # row 3: secondary status
        ui.add_grid_row_fill(_sid)           # row 4: spacer

        ui.create_label(f"bottom.status.{_sname}.title", _stitle, 10)
        ui.set_property_bool(f"bottom.status.{_sname}.title", "enabled", False)
        ui.create_label(f"bottom.status.{_sname}.port", "Port: —", 10)
        ui.set_property_bool(f"bottom.status.{_sname}.port", "enabled", False)

        _dot_id = f"bottom.status.{_sname}.dot"
        ui.create_widget(_dot_id, "demo.widgets.status_dot")
        ui.set_property_string(_dot_id, "foreground_color", "#666666")
        ui.create_label(f"bottom.status.{_sname}.label", "Offline", 10)
        ui.set_property_bool(f"bottom.status.{_sname}.label", "enabled", False)

        _dot2_id = f"bottom.status.{_sname}.dot2"
        ui.create_widget(_dot2_id, "demo.widgets.status_dot")
        ui.set_property_string(_dot2_id, "foreground_color", "#666666")
        ui.create_label(f"bottom.status.{_sname}.label2", "—", 10)
        ui.set_property_bool(f"bottom.status.{_sname}.label2", "enabled", False)

        if _sname == "host":
            _ping1_id = f"bottom.status.{_sname}.ping"
            _ping2_id = f"bottom.status.{_sname}.ping2"
            ui.create_widget(_ping1_id, "demo.widgets.status_dot")
            ui.set_property_string(_ping1_id, "foreground_color", "#666666")
            ui.create_widget(_ping2_id, "demo.widgets.status_dot")
            ui.set_property_string(_ping2_id, "foreground_color", "#666666")
        else:
            ui.create_button(f"bottom.status.{_sname}.kill", "Kill",
                             f"bottom.status.{_sname}.kill")
            ui.set_property_number(f"bottom.status.{_sname}.kill", "font_size", 10)

        ui.place_grid_child(_sid, f"bottom.status.{_sname}.title",  0, 0, 5, 1)
        if _sname != "host":
            ui.place_grid_child(_sid, f"bottom.status.{_sname}.kill",   5, 0)
        ui.place_grid_child(_sid, f"bottom.status.{_sname}.port",   1, 1, 5, 1)
        ui.place_grid_child(_sid, _dot_id,                           1, 2)
        ui.place_grid_child(_sid, f"bottom.status.{_sname}.label",  3, 2)
        ui.place_grid_child(_sid, _dot2_id,                          1, 3)
        ui.place_grid_child(_sid, f"bottom.status.{_sname}.label2", 3, 3)
        if _sname == "host":
            ui.place_grid_child(_sid, f"bottom.status.{_sname}.ping",  5, 2)
            ui.place_grid_child(_sid, f"bottom.status.{_sname}.ping2", 5, 3)
        ui.place_grid_child("bottom.status_panel", _sid, _col_idx, 0)

    ui.set_property_bool("bottom.host_io_output", "visible", False)
    ui.set_property_bool("bottom.console_panel", "visible", False)
    ui.set_property_bool("bottom.terminal_panel", "visible", False)
    ui.set_property_bool("bottom.status_panel", "visible", False)
    ui.place_grid_child("bottom.panel", "bottom.toolbar", 0, 0)
    ui.place_grid_child("bottom.panel", "bottom.build_output", 0, 1)
    ui.place_grid_child("bottom.panel", "bottom.host_io_output", 0, 1)
    ui.place_grid_child("bottom.panel", "bottom.console_panel", 0, 1)
    ui.place_grid_child("bottom.panel", "bottom.terminal_panel", 0, 1)
    ui.place_grid_child("bottom.panel", "bottom.status_panel", 0, 1)
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

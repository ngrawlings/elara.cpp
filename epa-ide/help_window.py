import os
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder

_DOCUMENTS_DIR = Path(__file__).resolve().parent / "documents"

# Tree structure: (node_id, label, doc_file | None, children)
_TREE = [
    ("help.e", "E Language", None, [
        ("help.e.overview",         "Overview",           "e_overview.md"),
        ("help.e.kernels_workers",  "Kernels and Workers","e_kernels_workers.md"),
        ("help.e.types_structs",    "Types and Structs",  "e_types_structs.md"),
        ("help.e.signals",          "Signals",            "e_signals.md"),
    ]),
    ("help.epa", "EPA", None, [
        ("help.epa.overview",      "Overview",           "epa_overview.md"),
        ("help.epa.vm_arch",       "VM Architecture",    "epa_vm_architecture.md"),
    ]),
    ("help.cpp", "C++ Host", None, [
        ("help.cpp.overview",      "Overview",           "cpp_host_overview.md"),
        ("help.cpp.interconnect",  "UI RPC Interconnect","cpp_host_interconnect.md"),
    ]),
    ("help.python", "External Python Logic", None, [
        ("help.python.overview",   "Overview",           "python_logic_overview.md"),
        ("help.python.connecting", "Connecting",         "python_logic_connecting.md"),
    ]),
]

# Flat map: node_id -> doc filename
_DOC_MAP: dict[str, str] = {}

def _build_doc_map():
    for _id, _label, _doc, children in _TREE:
        if _doc:
            _DOC_MAP[_id] = _doc
        for cid, _clabel, cdoc in children:
            if cdoc:
                _DOC_MAP[cid] = cdoc

_build_doc_map()


def _tree_items() -> list:
    items = []
    for node_id, label, _, children in _TREE:
        child_nodes = [
            {"id": cid, "label": clabel}
            for cid, clabel, _ in children
        ]
        items.append({
            "id": node_id,
            "label": label,
            "expanded": True,
            "children": child_nodes,
        })
    return items


def load_doc(node_id: str) -> str:
    filename = _DOC_MAP.get(node_id)
    if not filename:
        return _welcome_text()
    path = _DOCUMENTS_DIR / filename
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        return f"# Document Not Found\n\nCould not load `{filename}`.\n"


def _welcome_text() -> str:
    return (
        "# EPA-IDE Help\n\n"
        "Select a topic from the tree on the left to read its documentation.\n\n"
        "## Available Sections\n\n"
        "- **E Language** — the E source language: kernels, workers, types, and signals\n"
        "- **EPA** — the EPA VM, its architecture, and the E3SB scene binary format\n"
        "- **C++ Host** — the host application layer and the UI RPC interconnect\n"
        "- **External Python Logic** — connecting Python scripts to a running project\n"
    )


def default_node_id() -> str:
    return ""


def build_help_window() -> UiDocumentBuilder:
    ui = UiDocumentBuilder()
    ui.create_window("EPA-IDE Help", 960, 680, "org.elara.ui.epa-ide.help")
    ui.set_theme_mode("dark")

    # Root shell: nav tree (230px, resizable) | content (fill)
    ui.create_grid("help.shell")
    ui.add_grid_column_exact("help.shell", 230)
    ui.add_grid_column_fill("help.shell")
    ui.add_grid_row_exact("help.shell", 36)   # 0  title bar
    ui.add_grid_row_fill("help.shell")         # 1  main area

    # Title bar
    ui.create_grid("help.titlebar")
    ui.add_grid_column_exact("help.titlebar", 12)
    ui.add_grid_column_fill("help.titlebar")
    ui.add_grid_column_exact("help.titlebar", 80)
    ui.add_grid_column_exact("help.titlebar", 8)
    ui.add_grid_row_fill("help.titlebar")
    ui.create_label("help.title_label", "EPA-IDE Help", 14)
    ui.create_button("help.close_btn", "Close", "help.close")
    ui.place_grid_child("help.titlebar", "help.title_label", 1, 0)
    ui.place_grid_child("help.titlebar", "help.close_btn", 2, 0)

    # Nav tree panel
    ui.create_grid("help.nav_panel")
    ui.add_grid_column_fill("help.nav_panel")
    ui.add_grid_row_exact("help.nav_panel", 26)
    ui.add_grid_row_fill("help.nav_panel")
    ui.create_label("help.nav_title", "Topics", 12)
    ui.create_tree_view("help.nav_tree", hover_only=False)
    ui.set_property_number("help.nav_tree", "font_size", 13)
    ui.set_section_json("help.nav_tree", "nodes", _tree_items())
    ui.place_grid_child("help.nav_panel", "help.nav_title", 0, 0)
    ui.place_grid_child("help.nav_panel", "help.nav_tree", 0, 1)

    # Content panel (read-only markdown rich text editor)
    ui.create_widget("help.content", "elara.widgets.rich_text_edit")
    ui.set_property_string("help.content", "text", _welcome_text())
    ui.set_property_string("help.content", "read_only", "true")
    ui.set_property_string("help.content", "markdown", "true")
    ui.set_property_number("help.content", "font_size", 14)

    # Assemble
    ui.place_grid_child("help.shell", "help.titlebar",  0, 0, 2, 1)
    ui.place_grid_child("help.shell", "help.nav_panel", 0, 1)
    ui.place_grid_child("help.shell", "help.content",   1, 1)
    ui.set_grid_column_border_resizable("help.shell", 0, True)
    ui.set_root_content("help.shell")

    return ui

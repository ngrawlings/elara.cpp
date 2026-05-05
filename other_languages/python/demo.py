from .builder import UiDocumentBuilder


def build_demo_document() -> UiDocumentBuilder:
    ui = UiDocumentBuilder()
    ui.create_window("libElaraUI Demo", 800, 600, "org.elara.ui.demo")
    ui.set_theme_mode("light")

    ui.create_popup("demo.popup")
    ui.add_popup_item("demo.popup", "file.new", "New")
    ui.add_popup_item("demo.popup", "file.open", "Open")
    ui.add_popup_item("demo.popup", "file.save", "Save")
    ui.add_popup_item("demo.popup", "file.quit", "Quit")

    ui.create_tabs("demo.tabs")
    ui.set_root_content("demo.tabs")
    ui.push_root_popup("demo.popup")

    ui.create_surface_panel("demo.surface")
    ui.add_tab("demo.tabs", "Surface", "demo.surface")

    ui.create_grid("demo.widgets.grid")
    ui.add_tab("demo.tabs", "Widgets", "demo.widgets.grid")
    ui.add_grid_column_exact("demo.widgets.grid", 24)
    ui.add_grid_column_fill("demo.widgets.grid")
    ui.add_grid_column_exact("demo.widgets.grid", 160)
    ui.add_grid_column_exact("demo.widgets.grid", 24)
    ui.add_grid_row_exact("demo.widgets.grid", 24)
    ui.add_grid_row_exact("demo.widgets.grid", 46)
    ui.add_grid_row_exact("demo.widgets.grid", 38)
    ui.add_grid_row_exact("demo.widgets.grid", 38)
    ui.add_grid_row_exact("demo.widgets.grid", 38)
    ui.add_grid_row_fill("demo.widgets.grid")
    ui.add_grid_row_exact("demo.widgets.grid", 24)

    ui.create_label("demo.widgets.title", "Grid layout demo: label + button", 16)
    ui.create_button("demo.widgets.button", "Press Me", "grid.demo.press")
    ui.create_label("demo.widgets.input_label", "Name:", 14)
    ui.create_text_input("demo.widgets.input", "type here later", "")
    ui.create_checkbox("demo.widgets.check", "Enable numeric increment", True).set_property_number(
        "demo.widgets.check", "font_size", 14
    )
    ui.create_spinner("demo.widgets.spinner", 0, 10, 3, 1).set_property_number("demo.widgets.spinner", "font_size", 14)
    ui.create_radio_button("demo.widgets.radio.alpha", "Mode A", "demo.mode", True).set_property_number(
        "demo.widgets.radio.alpha", "font_size", 14
    )
    ui.create_radio_button("demo.widgets.radio.beta", "Mode B", "demo.mode", False).set_property_number(
        "demo.widgets.radio.beta", "font_size", 14
    )
    ui.create_slider("demo.widgets.hslider", "horizontal", 0, 100, 42, 1)
    ui.create_slider("demo.widgets.vslider", "vertical", 0, 1, 0.65, 0.05)

    ui.place_grid_child("demo.widgets.grid", "demo.widgets.title", 1, 1)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.button", 2, 1)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.input_label", 1, 2)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.input", 2, 2)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.check", 1, 3)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.spinner", 2, 3)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.radio.alpha", 1, 4)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.radio.beta", 2, 4)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.hslider", 1, 5)
    ui.place_grid_child("demo.widgets.grid", "demo.widgets.vslider", 2, 5)

    ui.create_density_map("demo.density")
    ui.set_property_number("demo.density", "base_capacity", 8)
    ui.set_property_number("demo.density", "capacity_multiplier", 2)
    ui.set_property_number("demo.density", "layer_count", 16)
    ui.set_section_json(
        "demo.density",
        "demo_data",
        {
            "type": "modulo_sequence",
            "sample_count": 65536,
            "sample_multiplier": 2,
        },
    )
    ui.add_tab("demo.tabs", "Density map", "demo.density")

    ui.create_multi_axis_line_chart("demo.chart")
    ui.set_property_string("demo.chart", "title", "Orders and latency by sample")
    ui.set_property_bool("demo.chart", "show_points", True)
    ui.set_section_json(
        "demo.chart",
        "demo_data",
        {
            "axes": [
                {
                    "id": "orders",
                    "label": "orders/min",
                    "side": "left",
                    "min": 0,
                    "max": 240,
                    "color": [0.15, 0.55, 0.95],
                },
                {
                    "id": "latency",
                    "label": "latency ms",
                    "side": "right",
                    "min": 0,
                    "max": 700,
                    "color": [0.95, 0.45, 0.20],
                },
            ],
            "series": [
                {
                    "id": "created",
                    "label": "created orders",
                    "axis": "orders",
                    "color": [0.13, 0.67, 0.94],
                    "values": [88, 102, 116, 124, 138, 149, 162, 176, 184, 196, 214, 228],
                },
                {
                    "id": "fulfilled",
                    "label": "fulfilled orders",
                    "axis": "orders",
                    "color": [0.12, 0.82, 0.48],
                    "values": [72, 86, 95, 109, 121, 132, 146, 153, 168, 177, 191, 205],
                },
                {
                    "id": "p95",
                    "label": "p95 latency",
                    "axis": "latency",
                    "color": [0.93, 0.40, 0.18],
                    "values": [410, 384, 360, 332, 318, 290, 304, 346, 372, 420, 465, 492],
                },
            ],
        },
    )
    ui.add_tab("demo.tabs", "Chart", "demo.chart")

    ui.create_rich_text_edit(
        "demo.editor",
        "# Elara Notes\n## Build status\n- RPC server is live\n- Sliders are now primitive widgets\n- Chart tab renders dual-axis sample data\n\n## Next step\nUse this editor as the basis for higher-level document widgets.\n\nHorizontal scrolling is handled by the child slider when lines grow beyond the viewport.",
    )
    ui.set_property_number("demo.editor", "font_size", 14)
    ui.add_tab("demo.tabs", "Editor", "demo.editor")

    ui.create_tree_view("demo.tree")
    ui.set_property_number("demo.tree", "font_size", 14)
    ui.set_section_json(
        "demo.tree",
        "nodes",
        [
            {
                "id": "workspace",
                "label": "workspace",
                "expanded": True,
                "children": [
                    {"id": "src", "label": "src", "expanded": True, "children": [{"id": "main.cpp", "label": "main.cpp"}]},
                    {"id": "docs", "label": "docs"},
                    {"id": "assets", "label": "assets"},
                ],
            }
        ],
    )
    ui.add_tab("demo.tabs", "Tree", "demo.tree")

    ui.create_list_view("demo.list")
    ui.set_property_number("demo.list", "font_size", 14)
    ui.set_section_json(
        "demo.list",
        "items",
        [
            {"id": "build", "label": "Build status"},
            {"id": "rpc", "label": "RPC session"},
            {"id": "widgets", "label": "Widget inventory"},
            {"id": "artifacts", "label": "Artifact output"},
        ],
    )
    ui.add_tab("demo.tabs", "List", "demo.list")

    return ui

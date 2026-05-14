>>>>>>>>>>main>>>>PROJECT_NAME>TARGET_NAME>SOCKET_ADDRESS>SOCKET_PORT>IS_RICH_EDITOR>INCLUDE_MULTI_CPU
import argparse
import json
import time
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder
from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError


def build_document():
    ui = UiDocumentBuilder()
    ui.create_window("%PROJECT_NAME%", 1080, 760, "org.elara.ui.%TARGET_NAME%")
    ui.set_theme_mode("light")
@include [%IS_RICH_EDITOR% == 1] app.py.rich_editor_document>>>>%PROJECT_NAME%
@include [%IS_RICH_EDITOR% == 0] app.py.tabbed_panel_document>>>>%PROJECT_NAME%
    return ui


@include [%INCLUDE_MULTI_CPU% == 1] app.py.start_background_worker>>>>

def main():
    parser = argparse.ArgumentParser(description="Load the generated Elara UI document into a running RPC head")
    parser.add_argument("--host", default="%SOCKET_ADDRESS%", help="RPC server host")
    parser.add_argument("--port", default=%SOCKET_PORT%, type=int, help="RPC server port")
    parser.add_argument("--snapshot", action="store_true", help="Fetch a root snapshot after loading")
    parser.add_argument("--output", help="Write the generated document JSON to this path")
    parser.add_argument("--once", action="store_true", help="Load once and exit immediately")
    parser.add_argument("--no-events", action="store_true", help="Do not subscribe to default UI events")
@include [%INCLUDE_MULTI_CPU% == 1] app.py.worker_arg>>>>
    args = parser.parse_args()

    def on_ui_event(params):
        print(json.dumps({"ui.event": params}, indent=2), flush=True)
        return {"received": True}

    builder = build_document()
    document_json = builder.to_json(indent=2)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(document_json, encoding="utf-8")
@include [%INCLUDE_MULTI_CPU% == 1] app.py.worker_init>>>>
    try:
        with ElaraUiRpcClient(args.host, args.port) as client:
            client.add_handler("ui.event", on_ui_event)
            load_result = client.load_document(builder)
            print(json.dumps(load_result, indent=2))
            if args.snapshot:
                snapshot = client.snapshot()
                print(json.dumps(snapshot, indent=2))
            if not args.no_events:
                for action in ("clicked", "keysTyped", "valueChanged", "keyDown", "keyUp", "action"):
                    client.enable_event(action)
            if args.once:
                return
@include [%INCLUDE_MULTI_CPU% == 1] app.py.worker_startup>>>>
            print("Connected to Elara UI RPC head. Press Ctrl+C to exit.", flush=True)
            while True:
                time.sleep(0.25)
    except KeyboardInterrupt:
@include [%INCLUDE_MULTI_CPU% == 1] app.py.worker_interrupt>>>>
        return
    except ElaraUiRpcError as exc:
        raise SystemExit(str(exc))


@include [%INCLUDE_MULTI_CPU% == 1] app.py.worker_finally>>>>

if __name__ == "__main__":
    main()
<<<<<<<<<<main

>>>>>>>>>>rich_editor_document>>>>PROJECT_NAME
    ui.create_grid("app.shell")
    ui.add_grid_column_fill("app.shell")
    ui.add_grid_row_exact("app.shell", 32)
    ui.add_grid_row_fill("app.shell")
    ui.set_root_content("app.shell")
    ui.create_menu_bar("app.menu")
    ui.set_property_number("app.menu", "font_size", 14)
    ui.set_menu_bar_menus("app.menu", [
        {"id": "file", "label": "&File", "items": [
            {"id": "file.new_file", "label": "&New File", "shortcut": "Ctrl+N"},
            {"id": "file.new_project", "label": "New &Project...", "shortcut": "Ctrl+Shift+N"},
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
            {"id": "help.about", "label": "&About %PROJECT_NAME%"}
        ]}
    ])
    ui.create_tabs("app.tabs")
    ui.create_rich_text_edit("app.editor", "# %PROJECT_NAME%\n\nThis template gives you a starting point for a document-oriented editor built on libElaraUI.\n\n- Connect backend actions over RPC\n- Extend the toolbar and outline tabs\n- Use snapshots to inspect state while iterating\n")
    ui.set_property_number("app.editor", "font_size", 14)
    ui.add_tab("app.tabs", "Editor", "app.editor")
    ui.create_list_view("app.outline")
    ui.set_property_number("app.outline", "font_size", 14)
    ui.set_section_json("app.outline", "items", [{"id": "draft", "label": "Draft notes"}, {"id": "tasks", "label": "Editing tasks"}, {"id": "publish", "label": "Publishing checklist"}])
    ui.add_tab("app.tabs", "Outline", "app.outline")
    ui.place_grid_child("app.shell", "app.menu", 0, 0)
    ui.place_grid_child("app.shell", "app.tabs", 0, 1)
<<<<<<<<<<rich_editor_document

>>>>>>>>>>tabbed_panel_document>>>>PROJECT_NAME
    ui.create_tabs("app.tabs")
    ui.set_root_content("app.tabs")
    ui.create_grid("app.panel")
    ui.add_tab("app.tabs", "Control Panel", "app.panel")
    ui.add_grid_column_exact("app.panel", 24)
    ui.add_grid_column_fill("app.panel")
    ui.add_grid_column_exact("app.panel", 220)
    ui.add_grid_row_exact("app.panel", 24)
    ui.add_grid_row_exact("app.panel", 44)
    ui.add_grid_row_exact("app.panel", 44)
    ui.add_grid_row_exact("app.panel", 44)
    ui.add_grid_row_fill("app.panel")
    ui.add_grid_row_exact("app.panel", 24)
    ui.create_label("app.title", "%PROJECT_NAME% control surface", 18)
    ui.create_text_input("app.endpoint", "service endpoint", "https://api.example.local")
    ui.create_button("app.refresh", "Refresh", "app.refresh")
    ui.create_checkbox("app.live", "Live updates", True).set_property_number("app.live", "font_size", 14)
    ui.create_spinner("app.interval", 1, 60, 5, 1).set_property_number("app.interval", "font_size", 14)
    ui.create_slider("app.risk", "horizontal", 0, 100, 35, 1)
    ui.create_list_view("app.activity")
    ui.set_property_number("app.activity", "font_size", 14)
    ui.set_section_json("app.activity", "items", [{"id": "queued", "label": "Queued refresh"}, {"id": "connected", "label": "Connected to RPC head"}, {"id": "ready", "label": "Ready for backend logic"}])
    ui.place_grid_child("app.panel", "app.title", 1, 1, 2, 1)
    ui.place_grid_child("app.panel", "app.endpoint", 1, 2)
    ui.place_grid_child("app.panel", "app.refresh", 2, 2)
    ui.place_grid_child("app.panel", "app.live", 1, 3)
    ui.place_grid_child("app.panel", "app.interval", 2, 3)
    ui.place_grid_child("app.panel", "app.risk", 1, 4, 2, 1)
    ui.place_grid_child("app.panel", "app.activity", 1, 5, 2, 1)
<<<<<<<<<<tabbed_panel_document

>>>>>>>>>>start_background_worker
def start_background_worker():
    from elara_ui.multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime
    ensure_multi_cpu_runtime(thread_count=2)
    worker = MultiCpuWorkerTemplate(str(Path(__file__).resolve().parent / "workers" / "worker_template.py"), ["8"])
    worker.start()
    return worker


<<<<<<<<<<start_background_worker

>>>>>>>>>>worker_arg
    parser.add_argument("--no-worker", action="store_true", help="Do not start the optional multi-core worker template")
<<<<<<<<<<worker_arg

>>>>>>>>>>worker_init
    worker = None
<<<<<<<<<<worker_init

>>>>>>>>>>worker_startup
            if not args.no_worker:
                try:
                    worker = start_background_worker()
                    print(json.dumps({"multi_cpu_worker": worker.snapshot()}, indent=2), flush=True)
                except RuntimeError as exc:
                    print(json.dumps({"multi_cpu_worker_disabled": str(exc)}, indent=2), flush=True)
<<<<<<<<<<worker_startup

>>>>>>>>>>worker_interrupt
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
<<<<<<<<<<worker_interrupt

>>>>>>>>>>worker_finally
    finally:
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass


<<<<<<<<<<worker_finally

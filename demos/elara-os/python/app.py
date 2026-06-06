import argparse
import json
import os
import time
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder
from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError
from elara_io import IoOp, build_default_chipset


def build_document():
    ui = UiDocumentBuilder()
    ui.create_window("ElaraOs", 1080, 760, "org.elara.ui.elara-os-python")
    ui.set_theme_mode("light")
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
    ui.create_label("app.title", "ElaraOs control surface", 18)
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
    return ui



def _run_as_external_logic(session_path: str) -> None:
    import ext_logic_client
    client = ext_logic_client.ExtLogicClient.from_session_file(session_path)
    client.connect_retry(timeout=10.0)
    client.register(name="elara-os-python")
    print("[ext-logic] Registered with IDE", flush=True)
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        client.close()


def main():
    elara_session = os.environ.get("ELARA_DEBUG_SESSION", "")
    if elara_session:
        _run_as_external_logic(elara_session)
        return

    parser = argparse.ArgumentParser(description="Load the generated Elara UI document into a running RPC head")
    parser.add_argument("--host", default="127.0.0.1", help="RPC server host")
    parser.add_argument("--port", default=18820, type=int, help="RPC server port")
    parser.add_argument("--snapshot", action="store_true", help="Fetch a root snapshot after loading")
    parser.add_argument("--output", help="Write the generated document JSON to this path")
    parser.add_argument("--once", action="store_true", help="Load once and exit immediately")
    parser.add_argument("--no-events", action="store_true", help="Do not subscribe to default UI events")
    parser.add_argument("--io-chipset-self-test", action="store_true", help="Run the virtual IO chipset draft self-test")
    args = parser.parse_args()

    if args.io_chipset_self_test:
        chipset = build_default_chipset()
        write_frame = chipset.request_from_host("storage", IoOp.WRITE, b"boot-sector\0elara-os")
        read_frame = chipset.request_from_host("storage", IoOp.READ, b"boot-sector")
        clock_frame = chipset.request_from_host("clock", IoOp.READ)
        for frame in (write_frame, read_frame, clock_frame):
            chipset.submit(frame)
        chipset.pump_all()
        replies = []
        while not chipset.egress.empty():
            reply = chipset.egress.get_nowait()
            replies.append({
                "device": reply.device,
                "op": reply.op.name,
                "sequence": reply.sequence,
                "payload": reply.payload.decode("utf-8", errors="replace"),
            })
        print(json.dumps({"devices": list(chipset.device_names()), "replies": replies}, indent=2))
        return

    def on_ui_event(params):
        print(json.dumps({"ui.event": params}, indent=2), flush=True)
        return {"received": True}

    builder = build_document()
    document_json = builder.to_json(indent=2)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(document_json, encoding="utf-8")
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
            print("Connected to Elara UI RPC head. Press Ctrl+C to exit.", flush=True)
            while True:
                time.sleep(0.25)
    except KeyboardInterrupt:
        return
    except ElaraUiRpcError as exc:
        raise SystemExit(str(exc))



if __name__ == "__main__":
    main()

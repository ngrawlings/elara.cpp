import argparse
import json
import os
import queue
import random
import struct
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import scrolledtext

from elara_io import (
    IoOp,
    PersistentBlockIoController,
    build_block_request,
    build_boot_device_list_ingress,
    build_default_chipset,
    default_virtual_drives,
)



def _ensure_ide_python_path() -> None:
    repo_root = Path(__file__).resolve().parents[3]
    ide_path = repo_root / "epa-ide"
    if ide_path.is_dir() and str(ide_path) not in sys.path:
        sys.path.insert(0, str(ide_path))


def _load_debug_session(session_path: str) -> dict:
    with open(session_path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _ingress_boot_descriptor(client) -> dict:
    drives = default_virtual_drives()
    PersistentBlockIoController(drives)
    payload = build_boot_device_list_ingress(drives)
    ingress_result = client.call(
        "elara.os.bootDescriptor",
        {
            "format": "BootDeviceList.flat_v1",
            "payload_hex": payload.hex(),
            "drives": [
                {
                    "drive_id": drive.drive_id,
                    "path": str(drive.path),
                    "mount_path": drive.mount_path,
                    "block_size": drive.block_size,
                    "block_count": drive.block_count,
                }
                for drive in drives
            ],
        },
        timeout=30.0,
    )

    return {
        "drives": [
            {
                "drive_id": drive.drive_id,
                "path": str(drive.path),
                "mount_path": drive.mount_path,
                "block_size": drive.block_size,
                "block_count": drive.block_count,
            }
            for drive in drives
        ],
        "payload_bytes": len(payload),
        "payload_hex": payload.hex(),
        "ingress": ingress_result,
    }


def _continue_boot_descriptor(client) -> dict:
    return client.call("elara.os.bootContinue", timeout=30.0)


class ExtLogicTkMonitor:
    def __init__(self, session_path: str) -> None:
        self.session_path = session_path
        self.root = tk.Tk()
        self.root.title("Elara OS Python Monitor")
        self.root.geometry("1120x760")
        self.status_var = tk.StringVar(value="Starting")
        self.event_queue: "queue.Queue[tuple[str, str]]" = queue.Queue()
        self.command_queue: "queue.Queue[str]" = queue.Queue()
        self.stop_event = threading.Event()
        self.last_host_status = ""
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        header = tk.Frame(self.root, padx=12, pady=12)
        header.pack(fill="x")
        tk.Label(header, text="EPA <-> C++ Host <-> Python", font=("TkDefaultFont", 14, "bold")).pack(anchor="w")
        tk.Label(header, textvariable=self.status_var, anchor="w").pack(fill="x", pady=(6, 0))

        controls = tk.Frame(self.root, padx=12, pady=0)
        controls.pack(fill="x", pady=(0, 12))
        tk.Button(controls, text="Power", width=12, command=lambda: self._queue_command("power")).pack(side="left")
        tk.Button(controls, text="Reset", width=12, command=lambda: self._queue_command("reset")).pack(side="left", padx=(8, 0))
        tk.Button(controls, text="Continue", width=12, command=lambda: self._queue_command("continue")).pack(side="left", padx=(8, 0))

        self.log = scrolledtext.ScrolledText(self.root, wrap="word", font=("TkFixedFont", 10))
        self.log.pack(fill="both", expand=True, padx=12, pady=(0, 12))
        self.log.configure(state="disabled")

    def run(self) -> None:
        worker = threading.Thread(target=self._worker_main, daemon=True)
        worker.start()
        self.root.after(100, self._drain_queue)
        self.root.mainloop()
        self.stop_event.set()

    def _set_status(self, text: str) -> None:
        self.event_queue.put(("status", text))

    def _append(self, text: str) -> None:
        self.event_queue.put(("log", text))

    def _queue_command(self, command: str) -> None:
        self.command_queue.put(command)
        self._append(f"[ui] queued {command} button press\n")

    def _drain_queue(self) -> None:
        while True:
            try:
                kind, payload = self.event_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                self.status_var.set(payload)
                continue
            self.log.configure(state="normal")
            self.log.insert("end", payload)
            self.log.see("end")
            self.log.configure(state="disabled")
        if not self.stop_event.is_set():
            self.root.after(100, self._drain_queue)

    def _on_close(self) -> None:
        self.stop_event.set()
        self.root.destroy()

    def _worker_main(self) -> None:
        _ensure_ide_python_path()
        import ext_logic_client

        client = ext_logic_client.ExtLogicClient.from_session_file(self.session_path)
        try:
            self._set_status("Connecting to IDE external logic bridge")
            client.connect_retry(timeout=10.0)
            client.register(name="elara-os-python")
            self._append("[python] registered with IDE ext-logic bridge\n")

            status = client.call("ext.debug.status", timeout=10.0)
            self.last_host_status = json.dumps(status, sort_keys=True)
            self._append(f"[python] host status: {json.dumps(status, indent=2)}\n")
            self._append("[python] waiting for Power, Reset, or Continue from Tk control panel\n")
            self._set_status("Connected - waiting for power")

            while not self.stop_event.is_set():
                try:
                    latest = client.call("ext.debug.status", timeout=5.0)
                    latest_key = json.dumps(latest, sort_keys=True)
                    if latest_key != self.last_host_status:
                        self.last_host_status = latest_key
                        self._append(f"[python] host status: {json.dumps(latest, indent=2)}\n")
                except Exception as exc:
                    self._append(f"[error] host status poll failed: {exc}\n")
                    self._set_status("Bridge error")
                    return

                try:
                    command = self.command_queue.get_nowait()
                except queue.Empty:
                    command = ""
                if command:
                    self._append(f"[action] handling {command} button\n")
                    if command == "continue":
                        self._set_status("Continuing queued EPA boot")
                        run_result = _continue_boot_descriptor(client)
                        self._append(f"[boot] host continue result:\n{json.dumps(run_result, indent=2)}\n")
                        self._append("[action] queued boot payload continued through EPA run\n")
                        self._set_status("Queued boot continued")
                    else:
                        self._set_status(f"Queueing {command} boot payload")
                        ingress = _ingress_boot_descriptor(client)
                        self._append(
                            "[boot] prepared BootDeviceList.flat_v1 "
                            f"bytes={ingress['payload_bytes']} "
                            f"hex_prefix={ingress['payload_hex'][:64]} "
                            f"drives={json.dumps([{'drive_id': drive['drive_id'], 'mount_path': drive['mount_path']} for drive in ingress['drives']])}\n"
                        )
                        self._append(f"[boot] host ingress result:\n{json.dumps(ingress['ingress'], indent=2)}\n")
                        self._append(f"[action] {command} boot payload queued through C++ host\n")
                        self._set_status(f"{command.capitalize()} payload queued")

                try:
                    pong = client.ping()
                except Exception as exc:
                    self._append(f"[error] bridge disconnect detected: {exc}\n")
                    self._set_status("Disconnected")
                    return
                if pong != "pong":
                    self._append(f"[error] unexpected ping response: {pong!r}\n")
                    self._set_status("Unexpected bridge response")
                    return
                time.sleep(1.0)
        except Exception as exc:
            self._append(f"[error] {exc}\n")
            self._set_status("Bridge error")
        finally:
            client.close()


def _run_standalone_tk_window() -> None:
    root = tk.Tk()
    root.title("Elara OS Python Monitor")
    root.geometry("920x620")

    header = tk.Frame(root, padx=12, pady=12)
    header.pack(fill="x")
    tk.Label(header, text="Elara OS Python/Tk Monitor", font=("TkDefaultFont", 14, "bold")).pack(anchor="w")
    tk.Label(
        header,
        text="No ELARA_DEBUG_SESSION is set. Launch this through the IDE debug flow to attach to the host bridge.",
        anchor="w",
        justify="left",
    ).pack(fill="x", pady=(6, 0))

    log = scrolledtext.ScrolledText(root, wrap="word", font=("TkFixedFont", 10))
    log.pack(fill="both", expand=True, padx=12, pady=(0, 12))
    log.insert(
        "end",
        "[python] standalone Tk mode\n"
        "[info] waiting for IDE-managed launch with ELARA_DEBUG_SESSION\n",
    )
    log.configure(state="disabled")
    root.mainloop()


def _run_as_external_logic(session_path: str) -> None:
    ExtLogicTkMonitor(session_path).run()


def run_io_chipset_self_test() -> dict:
    chipset = build_default_chipset()
    seed = time.time_ns()
    rng = random.Random(seed)
    test_block = bytes(rng.getrandbits(8) for _ in range(4096))
    write_frame = chipset.request_from_host("block_io", IoOp.WRITE, build_block_request(1, 0, 1, test_block))
    read_frame = chipset.request_from_host("block_io", IoOp.READ, build_block_request(1, 0, 1))
    clock_frame = chipset.request_from_host("clock", IoOp.READ)
    for frame in (write_frame, read_frame, clock_frame):
        chipset.submit(frame)
    chipset.pump_all()
    replies = []
    block_roundtrip_ok = False
    while not chipset.egress.empty():
        reply = chipset.egress.get_nowait()
        payload_view = reply.payload
        if reply.device == "block_io" and reply.sequence == 2:
            block_roundtrip_ok = reply.payload == test_block
        if reply.device == "clock" and reply.op.name == "ACK" and len(reply.payload) == 16:
            year, month, day, hour, minute, second, weekday, epoch_ns = struct.unpack("<HBBBBBBQ", reply.payload)
            payload_view = json.dumps({
                "year": year,
                "month": month,
                "day": day,
                "hour": hour,
                "minute": minute,
                "second": second,
                "weekday": weekday,
                "epoch_ns": epoch_ns,
            }).encode("utf-8")
        replies.append({
            "device": reply.device,
            "op": reply.op.name,
            "sequence": reply.sequence,
            "payload": (
                payload_view[:16].hex() + "..."
                if reply.device == "block_io" and reply.sequence == 2 and len(payload_view) > 32
                else payload_view.decode("utf-8", errors="replace")
            ),
        })
    return {
        "devices": list(chipset.device_names()),
        "seed": seed,
        "block_write_read_match": block_roundtrip_ok,
        "replies": replies,
    }


def main():
    elara_session = os.environ.get("ELARA_DEBUG_SESSION", "")
    if elara_session:
        _run_as_external_logic(elara_session)
        return

    parser = argparse.ArgumentParser(description="Run the Elara OS Python Tk monitor")
    parser.add_argument("--io-chipset-self-test", action="store_true", help="Run the virtual IO chipset draft self-test")
    args = parser.parse_args()

    if args.io_chipset_self_test:
        print(json.dumps(run_io_chipset_self_test(), indent=2))
        return
    _run_standalone_tk_window()



if __name__ == "__main__":
    main()

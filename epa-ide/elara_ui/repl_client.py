"""Small REPL client for controlling and dumping a running Elara UI head."""

from __future__ import annotations

import argparse
import json
import shlex
from pathlib import Path
from typing import Any, Mapping, Optional

from .rpc import ElaraUiRpcClient, ElaraUiRpcError
from .snapshot_dumper import UiSnapshotDumper


HELP = """
Commands:
  help                                  Show this help
  quit | exit                           Close the REPL
  snapshot [path]                       Dump full root widget tree/state to JSON
  snapshot-widget <widget-id> [path]    Dump one widget subtree to JSON
  print-snapshot                        Print full snapshot JSON to stdout
  call <method> [json-params]           Raw RPC call
  set-text <widget-id> <text>           Convenience wrapper around ui.setText
  focus <widget-id>                     Convenience wrapper around ui.setFocus
  enable <event-name>                   Enable an outbound UI event
  disable <event-name>                  Disable an outbound UI event
""".strip()


class ElaraUiRepl:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 18777,
        client_sections: Optional[Mapping[str, Mapping[str, Any]]] = None,
        default_snapshot_path: str | Path = "elara-ui-snapshot.json",
    ):
        self.client = ElaraUiRpcClient(host, port)
        self.dumper = UiSnapshotDumper(self.client, client_sections=client_sections)
        self.default_snapshot_path = Path(default_snapshot_path)
        self._running = False

    def run(self) -> int:
        self.client.connect()
        self._running = True
        print("Elara UI REPL connected. Type 'help' for commands.", flush=True)
        while self._running:
            try:
                line = input("elara-ui> ")
            except EOFError:
                print()
                break
            except KeyboardInterrupt:
                print()
                break
            self.execute_line(line)
        self.client.close()
        return 0

    def execute_line(self, line: str) -> Any:
        line = line.strip()
        if not line:
            return None
        try:
            parts = shlex.split(line)
        except ValueError as exc:
            print(f"parse_error: {exc}", flush=True)
            return None
        if not parts:
            return None
        cmd = parts[0]
        try:
            if cmd in ("quit", "exit"):
                self._running = False
                return None
            if cmd == "help":
                print(HELP, flush=True)
                return None
            if cmd == "snapshot":
                output = Path(parts[1]) if len(parts) > 1 else self.default_snapshot_path
                path = self.dumper.dump(output)
                print(json.dumps({"snapshot_written": str(path)}, indent=2), flush=True)
                return path
            if cmd == "snapshot-widget":
                if len(parts) < 2:
                    print("usage: snapshot-widget <widget-id> [path]", flush=True)
                    return None
                target = parts[1]
                output = Path(parts[2]) if len(parts) > 2 else Path(f"elara-widget-{target.replace('/', '_').replace('.', '_')}.json")
                path = self.dumper.dump_widget(target, output)
                print(json.dumps({"snapshot_written": str(path)}, indent=2), flush=True)
                return path
            if cmd == "print-snapshot":
                print(json.dumps(self.dumper.snapshot(), indent=2, ensure_ascii=False), flush=True)
                return None
            if cmd == "call":
                if len(parts) < 2:
                    print("usage: call <method> [json-params]", flush=True)
                    return None
                params = json.loads(parts[2]) if len(parts) > 2 else {}
                result = self.client.call(parts[1], params)
                print(json.dumps(result, indent=2, ensure_ascii=False), flush=True)
                return result
            if cmd == "set-text":
                if len(parts) < 3:
                    print("usage: set-text <widget-id> <text>", flush=True)
                    return None
                return self.client.set_text(parts[1], " ".join(parts[2:]))
            if cmd == "focus":
                if len(parts) != 2:
                    print("usage: focus <widget-id>", flush=True)
                    return None
                return self.client.set_focus(parts[1])
            if cmd == "enable":
                if len(parts) != 2:
                    print("usage: enable <event-name>", flush=True)
                    return None
                return self.client.enable_event(parts[1])
            if cmd == "disable":
                if len(parts) != 2:
                    print("usage: disable <event-name>", flush=True)
                    return None
                return self.client.disable_event(parts[1])
            print(f"unknown command: {cmd}", flush=True)
        except (ElaraUiRpcError, OSError, json.JSONDecodeError) as exc:
            print(f"error: {exc}", flush=True)
        return None


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Elara UI RPC REPL and snapshot dumper")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=18777, type=int)
    parser.add_argument("--snapshot-out", default="elara-ui-snapshot.json")
    parser.add_argument("--dump-on-connect", action="store_true")
    args = parser.parse_args(argv)

    repl = ElaraUiRepl(args.host, args.port, default_snapshot_path=args.snapshot_out)
    if args.dump_on_connect:
        repl.client.connect()
        path = repl.dumper.dump(args.snapshot_out)
        repl.client.close()
        print(json.dumps({"snapshot_written": str(path)}, indent=2), flush=True)
        return 0
    return repl.run()


if __name__ == "__main__":
    raise SystemExit(main())

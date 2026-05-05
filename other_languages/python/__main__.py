import argparse
import json
from pathlib import Path

from .demo import build_demo_document
from .rpc import ElaraUiRpcClient, ElaraUiRpcError


def main():
    parser = argparse.ArgumentParser(description="Build or load the Elara UI Python demo document")
    parser.add_argument("--output", help="Write the generated document JSON to this path")
    parser.add_argument("--host", default="127.0.0.1", help="RPC server host")
    parser.add_argument("--port", default=18777, type=int, help="RPC server port")
    parser.add_argument("--load", action="store_true", help="Load the generated document into a running Elara UI RPC server")
    parser.add_argument("--snapshot", action="store_true", help="Fetch a root snapshot after loading")
    args = parser.parse_args()

    builder = build_demo_document()
    document_json = builder.to_json(indent=2)

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(document_json, encoding="utf-8")
    else:
        print(document_json)

    if args.load:
        try:
            with ElaraUiRpcClient(args.host, args.port) as client:
                load_result = client.load_document(builder)
                print(json.dumps(load_result, indent=2))

                if args.snapshot:
                    snapshot = client.snapshot()
                    print(json.dumps(snapshot, indent=2))
        except ElaraUiRpcError as exc:
            raise SystemExit(str(exc))


if __name__ == "__main__":
    main()

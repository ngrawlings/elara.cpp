# Elara UI Python Client

This folder contains a Python-side client/logic layer for the Elara UI head.

It provides:
- a flat stateful JSON document builder
- a framed JSON-RPC client for the Elara UI RPC server
- a standalone demo entrypoint that builds the current demo document

Quick start:

```bash
cd /home/nyhl/workspace/elara.cpp
python3 -m other_languages.python --output /tmp/elara_demo.json
```

To load the generated document into a running server:

```bash
python3 -m other_languages.python --host 127.0.0.1 --port 18777 --load
```

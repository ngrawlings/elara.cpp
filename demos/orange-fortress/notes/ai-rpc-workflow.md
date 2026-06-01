# AI RPC Workflow Notes

These are working notes for using EPA-IDE's AI RPC without repeatedly opening
short-lived connections.

## Persistent Client

The persistent REPL client is:

```sh
python3 epa-ide/ai_rpc_client.py --port 18792
```

It keeps one TCP connection open, sends keepalive pings, and accepts either a
bare method name or a raw JSON request.

Useful forms:

```text
hello
ping
get_ui_status
get_project
ui_call {"method":"snapshot","params":{}}
{"method":"ping","params":{}}
```

Exit with:

```text
:quit
```

## Important Constraints

- The IDE AI RPC usually listens on `127.0.0.1:18792` in this setup.
- `elaraui-server` itself accepts a single UI RPC connection. Do not attach a
  second client directly to the IDE-owned UI server on `18777`; use AI RPC
  `ui_call` if driving the IDE UI.
- For an independent demo UI, launch a separate `elaraui-server` on a different
  port and a distinct backend/application id:

```sh
libElaraUI/build/bin/elaraui-server \
  --port 18798 \
  --backend-id org.elara.ui.orange-fortress.one-shot \
  --persistent
```

The `--backend-id` option was added so a second GTK application instance does
not collide with the running EPA-IDE UI server.

## Quick Checks

Check listeners:

```sh
ss -ltnp
```

Run the Orange Fortress one-shot host against the demo UI server:

```sh
cd demos/orange-fortress/cpp
./build/orange-fortress 127.0.0.1 18798
```

Expected success markers:

```text
Document loaded: {"loaded":true}
surface mailbox callback wid=1 len=4096
surface E3SB parsed: emitted=1 ...
ui.setSectionJson ok: target=app.surface section=commands ...
Scene confirm received from EPA scene worker.
```


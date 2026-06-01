# External Python Logic Overview

External Python logic is the third layer in an Elara project. It runs as a separate process and connects to the C++ host via an **ext-logic bridge** to observe state, react to events, and drive higher-level application behaviour.

## Role

The external Python logic layer handles:

- High-level event interpretation and state machines
- AI-assisted responses (LLM calls, embedding queries)
- Filesystem operations, network requests, and other I/O
- Scripted automation and test drivers

It does not render UI directly — all UI updates go through the C++ host via the ext-logic bridge, which relays them to `elaraui-server`.

## Connection

The C++ host publishes its ext-logic listener address when it starts. The ext-logic session file is written to a well-known location:

    /tmp/elara-ext-logic-<backend-id>.json

This file contains the host address and port. Python logic reads it to connect:

    with open(session_file) as f:
        session = json.load(f)
    sock.connect((session["host"], session["port"]))

Once connected the Python process registers itself and can send commands to the host or receive forwarded events.

## Communication Protocol

The ext-logic bridge uses newline-delimited JSON over TCP, the same shape as the AI RPC:

**Request:**

    {"method": "setText", "params": {"target": "app.label", "value": "Hello"}}

**Response:**

    {"ok": true, "result": {"updated": true}}

## What Python Logic Can Do

- Read widget state from the C++ host
- Call `ui.*` methods on the host to update the Elara UI
- Subscribe to forwarded EPA VM events (mailbox data, worker signals)
- Drive test scenarios end-to-end without a human at the keyboard

## Script Buttons

The IDE's **Script Buttons** panel reads `script-buttons.json` in the project root and exposes one-click buttons for common Python logic scripts. Each button specifies a script path and optional arguments.

## Session File and Attach

When the IDE launches a project it writes the ext-logic session file for that project. Python scripts can attach at any time by reading the session file — they do not need to be running from the start. Multiple Python processes can attach sequentially, but only one should be connected at a time.

# Project Alignment Process

## Rule

When Orange Fortress does not match what EPA-IDE expects, align the project.

Do not change EPA-IDE status panel logic, interconnect logic, or other IDE
behavior unless explicitly told to do so.

## Goal

Make the project conform to the IDE's existing runtime and debug contract so
the IDE status panel and debug tools work without IDE-specific patches.

## Process

1. Confirm the project metadata in `.elaraproject/project.json`.
2. Read the existing project-side contract already used by a working reference
   project such as `demos/epa-signal-lab`.
3. Compare Orange Fortress against that reference.
4. Add missing project-side wiring in Orange Fortress.
5. Rebuild the project.
6. Verify the project now reports the expected state through the IDE's existing
   mechanisms.

## What To Align

For Orange Fortress, alignment means matching the IDE debug-session contract:

- read `ELARA_DEBUG_SESSION` when launched by the IDE
- load the debug session descriptor
- use the IDE-provided UI RPC host/port when appropriate
- connect the C++ host to the IDE host debug bridge
- send a `register` event with PID and message
- answer IDE `ping` messages with `pong`
- send host `log` and `state` events
- advertise the C++ external logic listener with `ext_logic_listen`
- allow the Python side to attach through the existing ext-logic session file

## Preferred Reference

Use `demos/epa-signal-lab` as the reference implementation when Orange
Fortress needs to be brought into alignment with the IDE.

The IDE is treated as the fixed contract.

## What Not To Do

- Do not patch `epa-ide/app.py` just to make a project look green.
- Do not reinterpret status-panel meaning on a per-project basis.
- Do not change IDE bridge semantics for a single project.
- Do not add project-specific exceptions to the IDE unless explicitly asked.

## Verification

After alignment:

- the project should build successfully
- the C++ host should connect to the IDE bridge when launched through the IDE
- the host should respond to IDE pings
- the project should expose its ext-logic port to the IDE
- the Python side should be able to register through the ext-logic bridge
- the IDE status panel should reflect the project state without IDE changes

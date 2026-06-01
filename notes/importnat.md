# Importnat Notes

## UI Server Boundary

No logic can be inserted into `elaraui-server`. It is purely responsible for:

- rendering the UI state it is given
- returning raw input events

Application behavior, scene logic, input interpretation, and any other
non-rendering decisions must stay in the host/client side.

## Client Cleanup After Testing

All clients must be closed after testing.

Leaving old clients connected causes a string of clients to keep attempting to
connect to the UI server, which creates avoidable interference during later
tests and debugging.

After each test run, make sure any REPL clients, demo hosts, and helper
processes connected to the UI server are shut down cleanly before starting the
next run.

## IDE Status Panel Project Alignment

Do not touch the logic of the IDE status panel unless explicitly told to do so.

If the IDE status panel does not show the expected state for Orange Fortress,
align the project to conform to the IDE's existing contract instead of changing
the IDE.

## Confirmed Launch Order

Do not try to fix or redesign this yet. Use the confirmed launch order:

1. `elaraui-server`
2. `C++ Host`
3. `EPA VM`
4. `External logic`

When started in that order, all project lights should go green.

This was confirmed against the lab test demo.

## C++ Host UI Fallback

For the C++ host, if it cannot connect to the configured UI port, it must
launch `elaraui-server` itself on a random port and then connect to that port.

This means we will not manually launch `elaraui-server` for this flow. The
C++ host should bring up the UI server automatically when needed.

Assume `elaraui-server` is always available on the system `PATH`.

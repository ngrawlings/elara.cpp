# EpaSignalLab Host

This C++ UI RPC host loads `../build/epa.bin`, starts the EPA bundle, queues sample ingress into the `entry` kernel, and logs:

- ordinary `signal()` callbacks from the entry kernel
- host mailbox frames emitted by `frame_commit()`
- bundle/kernel status at startup

Quick start:
1. start the UI head with `./run-ui-head.sh`
2. build the client with `./build.sh`
3. build the EPA bundle into `../build/epa.bin`
4. launch the client with `./run-client.sh`

Use the on-screen buttons or stdin commands:

- `reload`
- `queue`
- `queue2`
- `snapshot`
- `clear`
- `quit`

The host is intentionally simple. Its main job is to provide a real app path for IDE and debugger work while exposing the signal/far-signal/host-mailbox behavior clearly.

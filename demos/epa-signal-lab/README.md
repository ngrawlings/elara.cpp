# EPA Signal Lab

`epa-signal-lab` is a deliberately small demo app for debugger and host-runtime work.

It demonstrates four communication paths in one bundle:

- local worker-to-worker GHS handoff with `next`
- worker-to-kernel notification with `signal()`
- worker-to-remote-kernel typed delivery with `far_signal(...)`
- worker-to-host mailbox delivery with `frame_begin(...)` + `frame_commit()`

## Layout

- `epa/entry.e`
  The entry kernel. Host ingress lands on `ingress_source`, is forwarded locally to `local_forward`, then fanned out to kernel, remote kernel, and host.
- `epa/remote_sink.e`
  A second kernel that receives the remote payload and sends back a typed ack.
- `cpp/`
  A generated Elara UI RPC C++ host shell adapted to load `build/epa.bin`, start the bundle, queue sample ingress, and log host/kernel events.

## Build

1. Build the C++ host:
   `cd cpp && ./build.sh`
2. Build the EPA bundle from the IDE with `Build -> Compile E/EPA`
   or compile `epa/entry.e` + `epa/remote_sink.e` into `build/epa.bin`.
3. Run the UI head from `cpp/run-ui-head.sh`
4. Run the host from `cpp/run-client.sh`

## Demo Flow

Queueing a sample ingress to worker `entry/1` causes:

1. `ingress_source` to hand the current GHS to `local_forward` with `next`
2. `local_forward` to `signal()` the entry kernel
3. `local_forward` to `far_signal()` a typed payload to `demo.signal_lab.remote`
4. `local_forward` to emit a host mailbox frame with `frame_commit()`
5. `remote_ingress` to send a typed ack back to the entry kernel

That gives one small app that is useful for both normal host execution and later full debugger-supervised host integration.

# C++ Host Overview

The C++ host is the application layer that ties the EPA VM, the Elara UI, and external logic together. It is responsible for:

- Starting the EPA VM and loading the `.bin` bundle
- Connecting to `elaraui-server` via BRPC
- Loading the UI document (widget tree)
- Ingressing payloads into the EPA VM
- Reading the VM mailbox and forwarding scene commands to the Vulkan surface
- Forwarding UI input events from the Elara server to the EPA VM

## Launch Sequence

The C++ host follows this startup order:

1. Parse arguments (UI server host/port, optional debug session env var)
2. Connect to `elaraui-server` (or launch it on a random port if none is running)
3. Load the EPA binary bundle
4. Load the UI document via `ui.loadDocument`
5. Send the initial scene or state to the Vulkan surface
6. Enter the event loop — poll mailbox, process UI events, ingest new payloads

## UI Fallback

If the host cannot connect to the configured UI port it launches `elaraui-server` itself on a random port and then connects:

    elaraui-server --port <random> --backend-id <app-backend-id> --persistent

This allows the host to bring up the full UI stack without requiring a separately running server.

## EPA VM Integration

The host creates an EPA VM instance, loads the compiled `.bin` bundle, and ingresses typed payloads into specific workers:

    EpaVm vm;
    vm.loadBin("build/epa.bin");
    vm.ingressPayload("orange.fortress.scene", 1, payload_bytes);

Results come back via the mailbox callback set before the event loop starts.

## Input Event Flow

The `elaraui-server` sends raw key and mouse events to the host over the BRPC connection. The C++ host is the only place where input interpretation happens — it maps raw keycodes to application actions and decides whether to ingest new payloads into the EPA VM or update cached scene state.

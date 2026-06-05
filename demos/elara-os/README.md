# Elara OS

Placeholder E kernels for an Elara-native operating system.

The goal is to grow this demo into a desktop-shaped proof that the EPA/E ISA can support an operating system without adopting the traditional Unix/process/address-space model.

## Shape

- `boot.e`: init-only boot kernel. Starts core services, publishes globals, then retires.
- `compositor.e`: display authority. Other kernels request surfaces and frame updates.
- `input_router.e`: input authority. Routes host input events to focused surfaces.
- `io_controller.e`: generic egress/ingress IO authority.
- `storage_controller.e`: disk/filesystem authority over framed read/write/list requests.
- `network_controller.e`: network/radio authority over framed send/receive requests.
- `security_authority.e`: identity, grants, and ACL-style capability decisions.
- `shell_desktop.e`: desktop shell and session authority.
- `app_surface.e`: minimal app-side surface client pattern.

These files are intentionally skeletal. They establish the component boundaries first; behavior can be added one kernel at a time.

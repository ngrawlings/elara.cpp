# Elara OS

Authority-shaped E kernels for an Elara-native operating system.

The goal is to grow this demo into a desktop-shaped proof that the EPA/E ISA can support an operating system without adopting the traditional Unix/process/address-space model.

## Technologies

- `epa/`: OS authority kernels and app-surface patterns.
- `cpp/`: simple Vulkan surface host that registers with the IDE status interconnect.
- `python/`: first draft virtual IO chipset for host-side device emulation.

## Shape

- `boot.e`: init-only boot kernel. Starts core services, publishes globals, then retires.
- `frame_authority.e`: display authority closest to the IO chipset. It owns the host-facing frame surface, paints the boot frame, and is the endpoint the window manager talks to for surface and present requests.
- `input_router.e`: input authority. Routes host input events to focused surfaces.
- `block_io_authority.e`: persistent block device authority. It owns the registered virtual drive IDs and is the only kernel allowed to speak in terms of raw blocks/LBAs.
- `filesystem_authority.e`: singular filesystem authority layered on top of block IO. It owns mounts and the namespace view exposed to shells and apps.
- `network_controller.e`: network/radio authority over framed send/receive requests.
- `security_authority.e`: identity, grants, and ACL-style capability decisions.
- `shell_desktop.e`: desktop shell and session authority.
- `app_surface.e`: minimal app-side surface client pattern.

The current bootstrap path now does three real jobs:

- emits the authority-owned boot frame
- registers the host-provided block devices with `elara.os.block_io`, including mount-point identities
- mounts the filesystem namespace through `elara.os.filesystem`

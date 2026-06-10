# Elara OS

Authority-shaped E kernels for an Elara-native operating system.

The goal is to grow this demo into a desktop-shaped proof that the EPA/E ISA can support an operating system without adopting the traditional Unix/process/address-space model.

## Technologies

- `epa/`: OS authority kernels organized by `io/` and `registry/` hierarchy.
- `cpp/`: simple Vulkan surface host that registers with the IDE status interconnect.
- `python/`: first draft virtual IO chipset for host-side device emulation.

## Shape

- `boot.e`: init-only boot kernel. Starts core services, publishes globals, then retires.
- `epa/io/frame/frame_io_authority.e`: display IO authority closest to the IO chipset. It owns the host-facing frame surface, paints the boot frame, and is the endpoint the window manager talks to for surface and present requests.
- `epa/io/hid/hid_io_authority.e`: HID IO authority. Routes keyboard, mouse, and other host HID events to focused surfaces.
- `epa/io/block/block_io_authority.e`: persistent block IO authority. It owns the registered virtual drive IDs and is the only kernel allowed to speak in terms of raw blocks/LBAs.
- `epa/io/filesystem/filesystem_authority.e`: singular filesystem IO authority layered on top of block IO. It owns mounts and the namespace view exposed to shells and apps.
- `epa/io/stream/network/network_controller.e`: network/radio controller over framed send/receive requests.
- `epa/registry/security/security_authority.e`: identity, grants, and ACL-style capability decisions.
- `epa/registry/shell/shell_desktop.e`: desktop shell and session authority.
- `epa/registry/app/app_surface.e`: minimal app-side surface client pattern.

The current bootstrap path now does three real jobs:

- emits the authority-owned boot frame
- registers the host-provided block devices with `elara.os.block_io`, including mount-point identities
- mounts the filesystem namespace through `elara.os.filesystem`

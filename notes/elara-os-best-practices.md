# Elara OS Launch, Boot, Debug, and Development Practices

This note captures the working practices that have emerged while bringing
`demos/elara-os` through its first boot-frame milestone. Keep it practical:
the goal is to make the next debugging session shorter than the last one.

## Source Map

Primary project files:

- `demos/elara-os/epa/*.e`: Elara OS authority kernels.
- `demos/elara-os/epa/*.em`: shared authority protocols.
- `demos/elara-os/AUTHORITY_MAP.md`: living authority topology map.
- `demos/elara-os/.elaraproject/source_files.json`: IDE/project source order.
- `demos/elara-os/cpp`: C++ host, EPA debug service, Vulkan surface host.
- `demos/elara-os/python`: external IO logic and boot descriptor generator.
- `~/.elaraos`: host-side virtual disk root used by the Python IO chipset.

Important authority entry points:

- `entry.e`: kernel entry and DynamicACLAuthority.
- `registry_authority.e`: Elara OS registry tree, analogous in spirit to Linux `/proc`.
- `boot.e`: initial boot descriptor ingress and boot-frame kickoff.
- `frame_authority.e`: authority that owns the host-facing frame surface.

## Build Order

When in doubt, rebuild in this order:

```sh
libElaraParallelAssembly/build/e/e2epabin --out demos/elara-os/build/epa.bin \
  demos/elara-os/epa/entry.e \
  demos/elara-os/epa/app_surface.e \
  demos/elara-os/epa/block_io_authority.e \
  demos/elara-os/epa/boot.e \
  demos/elara-os/epa/filesystem_authority.e \
  demos/elara-os/epa/frame_authority.e \
  demos/elara-os/epa/input_router.e \
  demos/elara-os/epa/network_controller.e \
  demos/elara-os/epa/registry_authority.e \
  demos/elara-os/epa/security_authority.e \
  demos/elara-os/epa/shell_desktop.e \
  demos/elara-os/epa/console_view.e \
  demos/elara-os/epa/window_manager.e
```

Then:

```sh
(cd demos/elara-os/python && ./build.sh)
(cd demos/elara-os/cpp && ./build.sh)
```

If EPA runtime code changes under `libElaraParallelAssembly/src`, rebuild and
install that archive before relinking the host:

```sh
(cd libElaraParallelAssembly && make install)
(cd demos/elara-os/cpp && ./build.sh)
```

## Launch Practice

Launch the C++ host directly during Elara OS work:

```sh
(cd demos/elara-os/cpp && ./run-client.sh)
```

The host should:

- choose or connect to an `elaraui-server` port
- load a Vulkan surface document
- show `Boot Pending` before EPA responds
- start the external logic server
- launch `demos/elara-os/python/app.py`
- write `/tmp/elara-os-ext-logic-session.json`

Before relaunching, kill stale host/UI/Python processes. Stale Python bridges
can keep old interconnect ports alive and make boot tests lie.

Useful check:

```sh
ps -ef | grep -E 'elara-os|elaraui-server|app.py|ext_logic' | grep -v grep
```

Then kill only the current Elara OS trio: `./build/elara-os`,
`elaraui-server ... org.elara.ui.elara-os...`, and `python3 ../python/app.py`.

## Boot Flow

The intended first boot flow is:

```text
Python external logic
  builds BootDeviceList.flat_v1 from ~/.elaraos virtual disks
  sends elara.os.bootDescriptor over external logic interconnect

C++ host
  loads demos/elara-os/build/epa.bin if needed
  pushes descriptor into boot.wid=1
  continues the boot pump

EPA authorities
  boot -> entry/DynamicACL registration
  boot -> frame_authority publish_boot_frame
  entry/DynamicACL -> registry_authority registration records
  frame_authority -> host mailbox egress frame

C++ host
  reads frame_authority mailbox
  converts EPA frame records to Vulkan surface commands
```

The host-side continue pump should run at least:

```text
boot
entry
registry_authority
frame_authority
```

That order matters. Boot emits registration and frame work; entry/DynamicACL
authorizes and forwards registry work; registry stores the path records; frame
authority emits the visible boot frame.

## External Logic Boot Test

Use this script shape for a direct boot test:

```sh
python3 - <<'PY'
import json, sys
sys.path.insert(0, 'epa-ide')
sys.path.insert(0, 'demos/elara-os/python')
from ext_logic_client import ExtLogicClient
from elara_io import PersistentBlockIoController, build_boot_device_list_ingress, default_virtual_drives

client = ExtLogicClient.from_session_file('/tmp/elara-os-ext-logic-session.json')
client.connect_retry(timeout=10.0)
try:
    drives = default_virtual_drives()
    PersistentBlockIoController(drives)
    payload = build_boot_device_list_ingress(drives)
    print(json.dumps(client.call('elara.os.bootDescriptor', {
        'format': 'BootDeviceList.flat_v1',
        'payload_hex': payload.hex(),
    }, timeout=30.0), indent=2))
    print(json.dumps(client.call('elara.os.bootContinue', timeout=30.0), indent=2)[:12000])
    print(json.dumps(client.call('ext.debug.status', timeout=10.0), indent=2))
finally:
    client.close()
PY
```

Expected stable milestone:

- `boot_payload_pending` becomes false after continue
- `boot` worker retires cleanly
- `registry_authority` reaches idle after path registrations
- `frame_authority` mailbox contains a valid EPA egress frame
- the surface leaves `Boot Pending`

## Debugging Practice

Prefer snapshots over guessing. For every failed `epa.debug.run`, surface:

- `path_id`
- stop reason
- worker list
- `retired`, `halted`, `blocked`, `faulted`, `waiting_for_data`
- `eip.block_type`, `eip.block_id`, `eip.rel_pc`
- stack depth and preview
- local arena top/cap

Interpret worker state carefully:

- `worker[0].halted` usually means the kernel block finished launching workers.
  It does not mean the authority is finished.
- `waiting_for_data` is an idle ingress state, not runnable work.
- `retired=1` on an ingress worker means that one-shot worker completed.
- `faulted=1` is the next thing to inspect; map `block_type/block_id/rel_pc`
  to generated EPA assembly.

Generated EPA assembly lives under:

```text
demos/elara-os/build/epaasm/*.epaasm
```

Use block IDs from snapshots to find generated code:

```sh
grep -n "FUNC_START 117" demos/elara-os/build/epaasm/entry.epaasm
```

Also inspect host stdout. Runtime faults often include clearer text than the
JSON snapshot, for example:

```text
[EPA-FAULT] kernel=... wid=... func[...] pc=... op=...
            detail: ...
```

## Current Hard-Won VM Lessons

The CPU-thread scheduler must not treat worker 0 halt as whole-kernel end while
authority workers are still alive. In E kernels, the kernel block is often just
the authority launcher.

The CPU-thread scheduler must not treat `waiting_for_data` workers as runnable.
Ingress workers waiting for payloads are stable idle.

Function return and CALL frames share the VM data stack today. `RET` must
preserve the function return value while popping the call frame, or zero-arg
helper calls such as protocol constants will corrupt the call frame.

Finite debug runs need a watchdog or an idle detector. The CPU-thread scheduler
can otherwise wait inside its management loop while the external logic RPC is
waiting for a response.

## Current Known Boot Blockers

As of the first registry/frame boot push:

- `boot` can ingest the hardware descriptor and retire cleanly.
- `registry_authority` can reach idle.
- `entry`/DynamicACL can fault while processing registration.
- `frame_authority` may not yet produce a committed mailbox frame from the full
  boot path.

Do not hide these with permanent host workarounds. Temporary fallback injection
is acceptable only as a diagnostic tool, and should be removed once far-signal,
DynamicACL registration, and frame mailbox egress are stable.

## Development Guidelines

Keep authority protocol changes in `.em` files and include them from each
authority. If `#include` breaks, fix the compiler/include path problem rather
than copying protocol definitions.

Keep the registry tree path-based:

```text
/system
/system/authorities
/system/authorities/frame
/system/authorities/registry
/system/authorities/dynacl
/devices
/devices/block
/mounts
/proc
```

Use hashed path-node keys for registry child lookup, but preserve readable path
intent in protocol names and documentation.

When adding authorities:

- add the `.e` file to `.elaraproject/source_files.json`
- add it to the EPA bundle build command
- add protocol definitions to a shared `.em`
- add a first-ingress registration with DynamicACLAuthority
- update `demos/elara-os/AUTHORITY_MAP.md`

When touching boot:

- keep `boot.e` init-only
- avoid long-running boot workers
- emit small typed protocol payloads
- let persistent authorities own persistent state

When touching FrameAuthority:

- keep host-facing frame egress owned by `frame_authority`
- only accept frames from the active manager once ConsoleView/WindowManager is active
- keep the boot frame path separate from manager-frame arbitration

## Cleanup

Generated or runtime files commonly seen during local sessions:

- `demos/elara-os/build/*`
- `demos/elara-os/.pids`
- `epa-ide/.pids`
- `/tmp/elara-os-ext-logic-session.json`
- `~/.elaraos/*`

Do not commit `.pids` files or accidental generated artifacts unless the project
explicitly starts tracking them.

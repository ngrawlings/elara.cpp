# EPA Frame Observability

The Orange Fortress host and EPA debug service now expose a stable IDE-facing
frame header for ingress and egress metrics.

Schema: `orange-fortress.epa.frame.v1`

Fields:

- `direction`: `ingress` or `egress`
- `valid`: header/parser status
- `magic`: `0x45465231` (`EFR1`) for frame-compatible traffic
- `version`: frame version, currently `1`
- `header_bytes`, `payload_bytes`, `total_bytes`
- `width`, `height`
- `frame_type`: `1` typed ingress payload, `3` Vulkan scene egress
- `frame_id`
- `record_count`
- `error`: `ok`, `bad_magic`, `unsupported_version`, or `truncated_header`

Host debug events:

- `ingress_frame`: emitted before the typed `ScenePoseInputPayload` is pushed
  into `orange.fortress.scene`.
- `egress_frame`: emitted when the host receives/parses an EPA mailbox frame.
- Existing `ingress` and `egress` events also include the same `frame` object.

EPA debug RPC:

- `epa.debug.getMailbox` returns `frame` beside `wid`, `len`, and `hex`.
- `epa.debug.clearMailbox` clears the captured mailbox before a new ingress run.

EPA logs:

- VM `log(...)` output captured by the debug service is routed as a `log` event
  and echoed to stdout with the prefix `[epa vm]`.

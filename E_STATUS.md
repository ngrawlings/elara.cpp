# E Compiler Output Status

Covers all `.e` source files found in the project and their compiled `.epaasm` output.
Hand-written reference EPA files (not compiler output) are listed separately at the bottom.

---

## Compiler fixes applied (2026-05-21)

The following gaps were fixed in `e_emit_epa.c` and `e_semantic.c`:

1. **Type body GHS field reads** — idents inside `type` bodies now resolve to `SET_R 2 offset / GR_MOV4 0 / PUSH R0` via the type layout. `current_type_layout` context is set when entering a type body.
2. **Type body local frames** — `e_semantic.c` now builds `vm_local` frames for type bodies so local variable declarations inside them get slots (previously the frame was always NULL → all locals produced comments).
3. **User-defined function calls** — `E_EXPR_CALL` now tries `emit_user_func_call` before falling to a comment. A pre-pass builds a `func_id_map` (name → FUNC_START id) for all `E_TOP_TYPE` and `E_TOP_FUNCTION` entries. Args with primitive int params are stored to the callee's local slots (STORE_L) before CALL.
4. **Statement call result discard** — when a user-defined function call is used as a statement and the function has a non-void return type, a `POP 0` is emitted to discard the return value.
5. **Switch compare-dispatch** — `E_STMT_SWITCH` on scalar targets (int locals/params) now emits a proper compare-and-branch chain. Non-scalar targets (type-ref dispatch) still emit a comment noting they are pending.
6. **FUNC_START frame_words** — type bodies now use `frame_words_for_function` instead of hardcoded 0.

---

## ✅ Valid EPA code

ENTRY/worker execution paths have real, substantive instructions. All type accessor FUNCs
now have real GHS field reads. No "pending lowering" or ident comments remain.

| E source | Output | Notes |
|---|---|---|
| `demos/orange-exterminator/epa/scene.e` | `build/epaasm/scene.epaasm` | Full kernel loop + 2 workers + type accessors with GHS reads |
| `demos/orange-exterminator/epa/player_avatar.e` | `build/epaasm/player_avatar.epaasm` | Player state machine; GHS reads, conditional branching |
| `demos/orange-exterminator/epa/render_scene.e` | `build/epaasm/render_scene.epaasm` | `.SSTR` string table; kernel + workers |
| `demos/orange-exterminator/epa/entry.e` | `build/epaasm/entry.epaasm` | Kernel: REQUEST_THREADS + WAIT_ON_SYNC loop |
| `demos/orange-exterminator/epa/gameplay_rules.e` | `build/epaasm/gameplay_rules.epaasm` | Same structure as entry |
| `demos/orange-exterminator/epa/render_ui.e` | `build/epaasm/render_ui.epaasm` | Kernel + 3 workers |
| `demos/orange-exterminator/epa/world_runtime.e` | `build/epaasm/world_runtime.epaasm` | Kernel + 2 workers |
| `demos/orange-exterminator/epa/walls.e` | `build/epaasm/walls.epaasm` | Kernel + 2 workers |
| `demos/orange-exterminator/epa/input_dispatch.e` | `build/epaasm/input_dispatch.epaasm` | Kernel + 2 workers |
| `demos/orange-exterminator/epa/player_machinegun.e` | `build/epaasm/player_machinegun.epaasm` | Kernel + 2 workers |
| `libElaraParallelAssembly/e/examples/ingress_hello.e` | `epa_build/ingress_hello.epaasm` | Type accessor, worker field reads, function call with CALL + STORE_L arg passing |

---

## ⚠️ Suspected bad EPA code

### `libElaraParallelAssembly/e/examples/seed.e`
Output: `epa_build/seed.epaasm`

- **Semantic error**: `struct Blob;` is a forward-declaration only — no GHS layout. The function
  `unchecked(Blob blob, int z)` requires a layout to compute parameter offsets. This causes a
  semantic analysis failure (`missing GHS layout for type 'Blob'`). The file **does not compile**
  with the current compiler.
- Additionally, `switch (packet)` switches on a type-ref worker param — this falls into the
  non-scalar switch path (comment only). Type-ref dispatch requires knowing the ingress type ID
  from the EPA runtime (e.g., R3 from `WORKER_TRX_IN_R 3`).
- Fix required: add a `type Blob(...)` declaration with fields, or remove the `unchecked` function.

---

## ❌ No output — not yet compiled

| E source | Reason |
|---|---|
| `demos/orange-exterminator/epa/test.e` | No `.epaasm` in `build/epaasm/`. File content is **identical** to `dynamic_memory.e` — appears to be a scratch copy. |
| `libElaraParallelAssembly/unittests/tests/dynamic_memory.e` | No `.epaasm` output found anywhere. Exercises `DYN_ALLOC`, `DYN_SWAP`, `DYN_ITER_HEAD/NEXT`, `DYN_FREE`, `dynamic_iterator`, `dynamic_next`, `static {}` initialiser blocks — recently added features. Not yet run through the compiler. |

---

## Hand-written reference EPA files

These are not compiler output — they are manually written test and template files.

| File | Real instructions | Status |
|---|---|---|
| `unittests/tests/at_entry.epaasm` | 25 | Valid — AT/WAIT\_FOR\_AT/SYNC round-trip |
| `unittests/tests/at_math_lifecycle.epaasm` | 74 | Valid — KERNEL\_TRX\_IN\_L, arithmetic, full lifecycle |
| `unittests/tests/const_test.epaasm` | 53 | Valid — .CONST\_STR + kernel loop |
| `unittests/tests/fmt_log_demo.epaasm` | 16 | Valid — formatted log demo with SIGNAL |
| `unittests/tests/ingress_to_mailbox.epaasm` | 17 | Valid — ingress → SIGNAL mailbox pattern |
| `unittests/tests/test.epaasm` | 49 | Valid — sequenced worker test with EXCEPT |
| `unittests/tests/test_template.epaasm` | 149 | Valid — reusable test-suite template (also duplicated at `unittests/test_template.epaasm`) |
| `unittests/tests/kernelv1.epaasm` | 0 | **Empty file** (0 bytes) |

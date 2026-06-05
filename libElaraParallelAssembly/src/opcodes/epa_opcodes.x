// epa_opcodes.x
// X(name, value, mnemonic, param_len_bytes_after_opcode)
//
// NOTE: param_len is BYTES AFTER the u8 opcode.
// EPA ISA v1 core opcodes stay below 128 so bit 7 is reserved.
// Values above the last listed opcode are reserved.
// If an instruction is variable length, use 0xFF.

//
// ---------- COMMON: Core / framing (lowest) ----------
//
X(NOOP,                    0u, "NOOP", 0)   // Does nothing (padding / alignment / placeholder)
X(SET_MODE,                1u, "SET_MODE", 1)   // Set VM execution mode flags (u8)
X(END,                     2u, "END", 0)   // End of current block (kernel / worker / func)

// Variable-length, top-level const data section (parsed by loader; not executed)
X(DATA_BLOCK,              3u, "DATA_BLOCK", 0xFF) // Constant pool (strings, numbers, bytes)

//
// ---------- COMMON: Control flow (branch / yield / entry scheduling) ----------
//
X(JMP_REL32,               4u, "JMP", 4) // Unconditional relative jump (i32 offset)
X(JZ_REL32,                5u, "JZ", 4) // Jump if top-of-stack == 0
X(JNZ_REL32,               6u, "JNZ", 4) // Jump if top-of-stack != 0
X(JLZ_REL32,               7u, "JLZ", 4) // Jump if top-of-stack < 0
X(JGZ_REL32,               8u, "JGZ", 4) // Jump if top-of-stack > 0
X(YIELD,                   9u, "YIELD", 1) // Yield execution (policy hint u8)

X(ENTRY_START,            10u, "ENTRY_START", 12) // Begin kernel/worker block (id, inw, outw)
X(ENTRY_END,              11u, "ENTRY_END", 0) // End entry block definition
X(ENTRY_EXEC,             12u, "ENTRY_EXEC", 1) // Execute worker by id (u8 wid)
X(ENTRY_HALT,             13u, "ENTRY_HALT", 1) // Halt worker by id (u8 wid)
X(DYNAMIC_POOL,           14u, "DYNAMIC_POOL", 20) // Program-level dynamic pool manifest (pool_id, element_size, min_free, max_free, grow_by)
X(KERNEL_ID,              15u, "KERNEL_ID", 8) // Program-level kernel 64-bit id (lo,hi)
X(ACL_ALLOW,              16u, "ACL_ALLOW", 12) // Program-level ACL allow (remote_id_lo, remote_id_hi, local_wid)
X(ENTRY_RETIRE,           17u, "ENTRY_RETIRE", 1) // Permanently remove worker from scheduler pool (u8 wid)
X(KERNEL_RETIRE,          18u, "KERNEL_RETIRE", 0) // Unload kernel by uid in r0/r1
X(ENTRY_PRIVILEGE,        19u, "ENTRY_PRIVILEGE", 5) // Initial privilege grant: u8 wid, u32 privilege
X(PRIVILEGE_LOCK,         20u, "PRIVILEGE_LOCK", 0) // Seal privilege grants for this loaded image
X(ACL_GRANT,              21u, "ACL_GRANT", 1) // Privileged dynamic ACL grant: r0/r1 target uid, r2/r3 remote uid, u8 local wid
X(ACL_REVOKE,             22u, "ACL_REVOKE", 1) // Privileged dynamic ACL revoke: r0/r1 target uid, r2/r3 remote uid, u8 local wid
X(ACL_REVOKE_ALL,         23u, "ACL_REVOKE_ALL", 0) // Privileged revoke all target routes for remote uid: r0/r1 target uid, r2/r3 remote uid
X(PID_SELF,               24u, "PID_SELF", 0) // Return current PID in r0; 0 means root/host-launched
X(PID_RETIRE,             25u, "PID_RETIRE", 0) // Retire PID in r0; self allowed, external requires privilege

X(SYNC,                   26u, "SYNC", 0) // Signal kernel from worker
X(WAIT_ON_SYNC,           27u, "WAIT_ON_SYNC", 0) // Kernel blocks until any SYNC arrives

X(WAIT_FOR_DATA,          28u, "WAIT_FOR_DATA", 0) // Block until input data available
X(DATA_READY,             29u, "DATA_READY", 1) // Signal data ready for worker (u8 wid)

//
// ---------- COMMON: Stack + arithmetic + locals/registers ----------
//
X(PUSH_I32,               30u, "PUSH_I32", 4)   // Push immediate i32 onto stack
X(PUSH_R,                 31u, "PUSH_R", 1)   // Push register value onto stack (u8 reg)
X(POP_R,                  32u, "POP", 1)   // Pop stack value into register (u8 reg)

X(ADD_I32,                33u, "ADD_I32", 0)   // Pop a,b → push (a+b)
X(SUB_I32,                34u, "SUB_I32", 0)   // Pop a,b → push (a-b)
X(MUL_I32,                35u, "MUL_I32", 0)   // Pop a,b → push (a*b)
X(LT_I32,                 36u, "LT_I32", 0)   // Pop a,b → push (a<b ? 1 : 0)
X(EQ_I32,                 37u, "EQ_I32", 0)   // Pop a,b → push (a==b ? 1 : 0)
X(NE_I32,                 38u, "NE_I32", 0)   // Pop a,b → push (a!=b ? 1 : 0)
X(LE_I32,                 39u, "LE_I32", 0)   // Pop a,b → push (a<=b ? 1 : 0)
X(GT_I32,                 40u, "GT_I32", 0)   // Pop a,b → push (a>b ? 1 : 0)
X(GE_I32,                 41u, "GE_I32", 0)   // Pop a,b → push (a>=b ? 1 : 0)
X(DIV_I32,                42u, "DIV_I32", 0)   // Pop a,b → push (a/b), b==0 yields 0

X(STORE_L,                43u, "STORE_L", 1)   // Store top-of-stack into local slot (u8 idx)  — worker use
X(LOAD_L,                 44u, "LOAD_L", 1)   // Load local slot and push to stack (u8 idx)   — worker use
X(STORE_LW,               45u, "STORE_LW", 4)   // Store top-of-stack into local slot (u32 idx) — function use
X(LOAD_LW,                46u, "LOAD_LW", 4)   // Load local slot and push to stack (u32 idx)  — function use

X(CMP,                    47u, "CMP", 0)   // Compare top two stack values (sets flags)
X(CMPZ,                   48u, "CMPZ", 0)   // Compare stack value against zero

X(MV,                     49u, "MV", 2)   // Copy register src → dst (u8 dst,u8 src)
X(SET_R,                  50u, "SET_R", 5)   // Set register to immediate i32
X(INC,                    51u, "INC", 1)   // Increment register
X(DEC,                    52u, "DEC", 1)   // Decrement register

// Local byte heap access (byte-granular)
X(RLB_MOV1,               53u, "RLB_MOV1", 2) // u8 reg, u8 lb_reg: lbytes[csc[lb_reg]] = csc[reg] & 0xFF (1 byte)
X(LBR_MOV1,               54u, "LBR_MOV1", 2) // u8 reg, u8 lb_reg: csc[reg] = lbytes[csc[lb_reg]] (1 byte, zero-ext)
X(RLB_MOV4,               55u, "RLB_MOV4", 2) // u8 reg, u8 lb_reg: lbytes[csc[lb_reg]] = csc[reg] (4 bytes LE)
X(LBR_MOV4,               56u, "LBR_MOV4", 2) // u8 reg, u8 lb_reg: csc[reg] = lbytes[csc[lb_reg]] (4 bytes LE)

X(SM_PUT,                 57u, "SM_PUT", 0) // write u32 from r0 to signal mailbox at r3; r3 += 4

X(LOAD_CONST,             58u, "LOAD_CONST", 4) // Load constant by id → r0,r1,r2,r3

//
// ---------- COMMON: Functions ----------
//
X(FUNC_START,             59u, "FUNC_START", 6)  // Begin function (func_id, frame_words)
X(FUNC_END,               60u, "FUNC_END", 0)  // End function
X(CALL,                   61u, "CALL", 4)  // Call function by id
X(RET,                    62u, "RET", 0)  // Return from function
X(AT_ENTRY_START,         63u, "AT_ENTRY_START", 6) // Begin AT entry (at_id, frame_words)
X(AT_ENTRY_END,           64u, "AT_ENTRY_END", 0) // End AT entry

//
// ---------- COMMON: Ring-buffer transfers ----------
//
X(KERNEL_TRX_IN_L,        65u, "KERNEL_TRX_IN_L", 7)  // Kernel pulls worker local → kernel
X(KERNEL_TRX_OUT_L,       66u, "KERNEL_TRX_OUT_L", 7)  // Kernel pushes kernel → worker local
X(WORKER_TRX_IN_L,        67u, "WORKER_TRX_IN_L", 6)  // Worker pulls kernel → local
X(WORKER_TRX_OUT_L,       68u, "WORKER_TRX_OUT_L", 6)  // Worker pushes local → kernel
X(WORKER_TRX_IN_R,        69u, "WORKER_TRX_IN_R", 1)  // Worker pulls kernel → register
X(WORKER_TRX_OUT_R,       70u, "WORKER_TRX_OUT_R", 1)  // Worker pushes register → kernel
X(WORKER_TRX,             71u, "WORKER_TRX", 10)  // Local-to-local transfer
X(KERNEL_GHS_IN_R,        72u, "KERNEL_GHS_IN_R", 4)  // Kernel pulls worker current GHS -> r0/r1, r2=ok

//
// ---------- COMMON: Global Handle Space ----------
//
X(G_ALLOC,                73u, "G_ALLOC", 0) // Allocate global handle (type,size)
X(G_FREE,                 74u, "G_FREE", 0) // Free global handle
X(G_XFER,                 75u, "G_XFER", 0) // Transfer ownership of handle
X(G_XFERX,                76u, "G_XFERX", 4) // new multi version
X(G_RESIZE,               77u, "G_RESIZE", 0) // Resize global handle
X(G_PTR,                  78u, "G_PTR", 0) // Get host pointer for handle (host only)
X(G_META,                 79u, "G_META", 0) // Query handle metadata
X(G_TAG,                  80u, "G_TAG", 0) // Query handle tag -> r0=tag

// Ingress ring helper: pop 4 u32 words from the worker INQ into r0..r3.
// Intended for the standard payload routing: INGRESS -> GHS -> notify worker,
// where the worker's INQ receives a 4-word coordinate packet.
X(GR_MOV4,                81u, "GR_MOV4", 1) // read GHS u32 at r0/r1 handle + r2 offset into selected reg
X(GW_MOV4,                82u, "GW_MOV4", 1) // write selected reg as u32 to GHS at r0/r1 handle + r2 offset
X(G_ALLOC_L,              83u, "G_ALLOC_L", 0) // allocate final-size GHS and copy r1 bytes from lbytes[r2], type r0
X(RGM_PUBLISH_L,          84u, "RGM_PUBLISH_L", 0) // publish read-only named global from local bytes: r0/r1=name uid, r2=local off, r3=size
X(RGM_GET,                85u, "RGM_GET", 0) // get read-only global handle by name uid in r0/r1 -> r0/r1 handle, r2=size, r3=ok
X(RGM_META,               86u, "RGM_META", 0) // r0/r1 handle -> r0=name_lo, r1=name_hi, r2=size, r3=gen
X(RGM_READ4,              87u, "RGM_READ4", 1) // read RGM u32 at r0/r1 handle + r2 offset into selected reg
X(DYN_ALLOC,              88u, "DYN_ALLOC", 4) // pool_id:u32 -> r0=id, r1=ok
X(DYN_FREE,               89u, "DYN_FREE", 4) // pool_id:u32, r0=id -> r1=ok
X(DYN_LOAD,               90u, "DYN_LOAD", 4) // pool_id:u32, r0=id -> r0=off,r1=size,r2=ok
X(DYN_STORE,              91u, "DYN_STORE", 4) // pool_id:u32, r0=id,r1=off,r2=size -> r3=ok
X(DYN_SWAP,               92u, "DYN_SWAP", 4) // pool_id:u32, r0=id_a,r1=id_b -> r2=ok
X(DYN_ITER_HEAD,          93u, "DYN_ITER_HEAD", 4) // pool_id:u32 -> r0=live_head slot_id
X(DYN_ITER_NEXT,          94u, "DYN_ITER_NEXT", 4) // pool_id:u32, r0=current_slot_id -> r0=next_slot_id, r1=ok, r2=off, r3=size


//
// ---------- COMMON: Debug / interrupts ----------
//
X(BREAK,                  95u, "BREAK", 4) // Debug break with code
X(TRAP,                   96u, "TRAP", 4) // Fatal VM trap
X(EXCEPT,                 97u, "EXCEPT", 4) // Raise exception (fatal by policy)
X(SIGNAL,                 98u, "SIGNAL", 0)
X(FAR_SIGNAL,             99u, "FAR_SIGNAL", 0)
X(HOST_SIGNAL,           100u, "HOST_SIGNAL", 0)
X(REQUEST_THREADS,       101u, "REQUEST_THREADS", 0) // kernel-only, uses r0=desired_total_threads
X(REQUEST_AT,            102u, "REQUEST_AT", 0) // copy stack AT descriptor, issue system AT request

// Local byte arena allocation (worker-local, byte-addressable)
X(L_ALLOC,               103u, "L_ALLOC", 0) // in: r0=size_bytes  out: r0=off, r1=size, r2=ok(1/0), r3=0
X(L_RESET,               104u, "L_RESET", 0) // reset worker local byte arena head to 0
X(L_SCOPE_ENTER,         105u, "L_SCOPE_ENTER", 0) // push current local byte arena head onto scope stack
X(L_SCOPE_LEAVE,         106u, "L_SCOPE_LEAVE", 0) // restore local byte arena head from scope stack
X(L_SCOPE_ALLOC,         107u, "L_SCOPE_ALLOC", 0) // like L_ALLOC, intended for scoped allocations

X(FMT,                   108u, "FMT", 1) // u8 argc
X(LOG,                   109u, "LOG", 0) // consumes (r0=off, r1=len, r2=kind)

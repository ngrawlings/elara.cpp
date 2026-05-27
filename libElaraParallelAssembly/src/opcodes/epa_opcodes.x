// epa_opcodes.x
// X(name, value, mnemonic, param_len_bytes_after_opcode)
//
// NOTE: param_len is BYTES AFTER the u16 opcode.
// If an instruction is variable length, use 0xFF.

// epa_opcodes.x
// X(name, value, mnemonic, param_len_bytes_after_opcode)
//
// NOTE: param_len is BYTES AFTER the u16 opcode.
// If an instruction is variable length, use 0xFF.

//
// ---------- COMMON: Core / framing (lowest) ----------
//
X(NOOP,        0x0000u, "NOOP",      0)   // Does nothing (padding / alignment / placeholder)
X(SET_MODE,    0x0001u, "SET_MODE",  1)   // Set VM execution mode flags (u8)
X(END,         0x0002u, "END",       0)   // End of current block (kernel / worker / func)

// Variable-length, top-level const data section (parsed by loader; not executed)
X(DATA_BLOCK,  0x0003u, "DATA_BLOCK", 0xFF) // Constant pool (strings, numbers, bytes)

//
// ---------- COMMON: Control flow (branch / yield / entry scheduling) ----------
//
X(JMP_REL32,      0x0100u, "JMP",          4) // Unconditional relative jump (i32 offset)
X(JZ_REL32,       0x0101u, "JZ",           4) // Jump if top-of-stack == 0
X(JNZ_REL32,      0x0102u, "JNZ",          4) // Jump if top-of-stack != 0
X(JLZ_REL32,      0x0103u, "JLZ",          4) // Jump if top-of-stack < 0
X(JGZ_REL32,      0x0104u, "JGZ",          4) // Jump if top-of-stack > 0
X(YIELD,          0x0110u, "YIELD",        1) // Yield execution (policy hint u8)

X(ENTRY_START,    0x0120u, "ENTRY_START",  12) // Begin kernel/worker block (id, inw, outw)
X(ENTRY_END,      0x0121u, "ENTRY_END",    0) // End entry block definition
X(ENTRY_EXEC,     0x0122u, "ENTRY_EXEC",   1) // Execute worker by id (u8 wid)
X(ENTRY_HALT,     0x0123u, "ENTRY_HALT",   1) // Halt worker by id (u8 wid)
X(DYNAMIC_POOL,   0x0124u, "DYNAMIC_POOL", 20) // Program-level dynamic pool manifest (pool_id, element_size, min_free, max_free, grow_by)
X(KERNEL_ID_STR,  0x0125u, "KERNEL_ID_STR", 4) // Program-level kernel id string const id
X(ACL_ALLOW,      0x0126u, "ACL_ALLOW",   12) // Program-level ACL allow (remote_id_lo, remote_id_hi, local_wid)

X(SYNC,           0x0130u, "SYNC",         0) // Signal kernel from worker
X(WAIT_ON_SYNC,   0x0131u, "WAIT_ON_SYNC", 0) // Kernel blocks until any SYNC arrives

X(WAIT_FOR_DATA,  0x0140u, "WAIT_FOR_DATA",0) // Block until input data available
X(DATA_READY,     0x0141u, "DATA_READY",   1) // Signal data ready for worker (u8 wid)

X(WAIT_FOR_AT,  0x0142u, "WAIT_FOR_AT",0) // Block until AT finished

X(AT,  0x0150u, "AT", 0) // Execute Atomic Task (register ABI)

//
// ---------- COMMON: Stack + arithmetic + locals/registers ----------
//
X(PUSH_I32,   0x0200u, "PUSH_I32", 4)   // Push immediate i32 onto stack
X(PUSH_R,     0x0201u, "PUSH_R",   1)   // Push register value onto stack (u8 reg)
X(POP_R,      0x0202u, "POP",      1)   // Pop stack value into register (u8 reg)

X(ADD_I32,    0x0210u, "ADD_I32",  0)   // Pop a,b → push (a+b)
X(SUB_I32,    0x0211u, "SUB_I32",  0)   // Pop a,b → push (a-b)
X(MUL_I32,    0x0212u, "MUL_I32",  0)   // Pop a,b → push (a*b)
X(LT_I32,     0x0213u, "LT_I32",   0)   // Pop a,b → push (a<b ? 1 : 0)
X(EQ_I32,     0x0214u, "EQ_I32",   0)   // Pop a,b → push (a==b ? 1 : 0)
X(NE_I32,     0x0215u, "NE_I32",   0)   // Pop a,b → push (a!=b ? 1 : 0)
X(LE_I32,     0x0216u, "LE_I32",   0)   // Pop a,b → push (a<=b ? 1 : 0)
X(GT_I32,     0x0217u, "GT_I32",   0)   // Pop a,b → push (a>b ? 1 : 0)
X(GE_I32,     0x0218u, "GE_I32",   0)   // Pop a,b → push (a>=b ? 1 : 0)
X(DIV_I32,    0x0219u, "DIV_I32",  0)   // Pop a,b → push (a/b), b==0 yields 0

X(STORE_L,    0x0220u, "STORE_L",  1)   // Store top-of-stack into local slot (u8 idx)  — worker use
X(LOAD_L,     0x0221u, "LOAD_L",   1)   // Load local slot and push to stack (u8 idx)   — worker use
X(STORE_LW,   0x0227u, "STORE_LW", 4)   // Store top-of-stack into local slot (u32 idx) — function use
X(LOAD_LW,    0x0228u, "LOAD_LW",  4)   // Load local slot and push to stack (u32 idx)  — function use

X(CMP,        0x0230u, "CMP",      0)   // Compare top two stack values (sets flags)
X(CMPZ,       0x0231u, "CMPZ",     0)   // Compare stack value against zero

X(MV,         0x0240u, "MV",       2)   // Copy register src → dst (u8 dst,u8 src)
X(SET_R,      0x0241u, "SET_R",    5)   // Set register to immediate i32
X(INC,        0x0242u, "INC",      1)   // Increment register
X(DEC,        0x0243u, "DEC",      1)   // Decrement register

// Local byte heap access (byte-granular)
X(RLB_MOV1, 0x0245u, "RLB_MOV1", 2) // u8 reg, u8 lb_reg: lbytes[csc[lb_reg]] = csc[reg] & 0xFF (1 byte)
X(LBR_MOV1, 0x0246u, "LBR_MOV1", 2) // u8 reg, u8 lb_reg: csc[reg] = lbytes[csc[lb_reg]] (1 byte, zero-ext)
X(RLB_MOV4, 0x0248u, "RLB_MOV4", 2) // u8 reg, u8 lb_reg: lbytes[csc[lb_reg]] = csc[reg] (4 bytes LE)
X(LBR_MOV4, 0x0249u, "LBR_MOV4", 2) // u8 reg, u8 lb_reg: csc[reg] = lbytes[csc[lb_reg]] (4 bytes LE)

X(SM_PUT, 0x0247u, "SM_PUT", 0) // write u32 from r0 to signal mailbox at r3; r3 += 4

X(LOAD_CONST, 0x0244u, "LOAD_CONST", 4) // Load constant by id → r0,r1,r2,r3

//
// ---------- COMMON: Functions ----------
//
X(FUNC_START, 0x002Au, "FUNC_START", 6)  // Begin function (func_id, frame_words)
X(FUNC_END,   0x002Bu, "FUNC_END",   0)  // End function
X(CALL,       0x002Cu, "CALL",       4)  // Call function by id
X(RET,        0x002Du, "RET",        0)  // Return from function

//
// ---------- COMMON: Ring-buffer transfers ----------
//
X(KERNEL_TRX_IN_L,   0x0300u, "KERNEL_TRX_IN_L",   7)  // Kernel pulls worker local → kernel
X(KERNEL_TRX_OUT_L,  0x0301u, "KERNEL_TRX_OUT_L",  7)  // Kernel pushes kernel → worker local
X(WORKER_TRX_IN_L,   0x0302u, "WORKER_TRX_IN_L",   6)  // Worker pulls kernel → local
X(WORKER_TRX_OUT_L,  0x0303u, "WORKER_TRX_OUT_L",  6)  // Worker pushes local → kernel
X(WORKER_TRX_IN_R,   0x0304u, "WORKER_TRX_IN_R",   1)  // Worker pulls kernel → register
X(WORKER_TRX_OUT_R,  0x0305u, "WORKER_TRX_OUT_R",  1)  // Worker pushes register → kernel
X(WORKER_TRX,        0x0306u, "WORKER_TRX",       10)  // Local-to-local transfer
X(KERNEL_GHS_IN_R,   0x0307u, "KERNEL_GHS_IN_R",   4)  // Kernel pulls worker current GHS -> r0/r1, r2=ok

//
// ---------- COMMON: Global Handle Space ----------
//
X(G_ALLOC,   		0x0310u, "G_ALLOC",   	0) // Allocate global handle (type,size)
X(G_FREE,    		0x0311u, "G_FREE",    	0) // Free global handle
X(G_XFER,    		0x0312u, "G_XFER",    	0) // Transfer ownership of handle
X(G_XFERX, 			0x0313u, "G_XFERX", 	4) // new multi version
X(G_RESIZE,  		0x0314u, "G_RESIZE",  	0) // Resize global handle
X(G_PTR,     		0x0315u, "G_PTR",     	0) // Get host pointer for handle (host only)
X(G_META,    		0x0316u, "G_META",    	0) // Query handle metadata
X(G_TAG,            0x0318u, "G_TAG",       0) // Query handle tag -> r0=tag

// Ingress ring helper: pop 4 u32 words from the worker INQ into r0..r3.
// Intended for the standard payload routing: INGRESS -> GHS -> notify worker,
// where the worker's INQ receives a 4-word coordinate packet.
X(GR_MOV4,         0x0317u, "GR_MOV4", 1) // out: r0..r3 (u32 each)
X(DYN_ALLOC,       0x0319u, "DYN_ALLOC", 4) // pool_id:u32 -> r0=id, r1=ok
X(DYN_FREE,        0x031Au, "DYN_FREE",  4) // pool_id:u32, r0=id -> r1=ok
X(DYN_LOAD,        0x031Bu, "DYN_LOAD",  4) // pool_id:u32, r0=id -> r0=off,r1=size,r2=ok
X(DYN_STORE,       0x031Cu, "DYN_STORE", 4) // pool_id:u32, r0=id,r1=off,r2=size -> r3=ok
X(DYN_SWAP,        0x031Du, "DYN_SWAP",  4) // pool_id:u32, r0=id_a,r1=id_b -> r2=ok
X(DYN_ITER_HEAD,   0x031Eu, "DYN_ITER_HEAD", 4) // pool_id:u32 -> r0=live_head slot_id
X(DYN_ITER_NEXT,   0x031Fu, "DYN_ITER_NEXT", 4) // pool_id:u32, r0=current_slot_id -> r0=next_slot_id, r1=ok, r2=off, r3=size


//
// ---------- COMMON: Debug / interrupts ----------
//
X(BREAK,   0x0400u, "BREAK",  4) // Debug break with code
X(TRAP,    0x0401u, "TRAP",   4) // Fatal VM trap
X(EXCEPT,  0x0402u, "EXCEPT", 4) // Raise exception (fatal by policy)
X(SIGNAL,  0x0403u, "SIGNAL", 0)
X(FAR_SIGNAL,  0x0404u, "FAR_SIGNAL", 0)
X(HOST_SIGNAL, 0x0405u, "HOST_SIGNAL", 0)
X(REQUEST_THREADS, 0x0406u, "REQUEST_THREADS", 0) // kernel-only, uses r0=desired_total_threads

// Local byte arena allocation (worker-local, byte-addressable)
X(L_ALLOC, 0x0222u, "L_ALLOC", 0) // in: r0=size_bytes  out: r0=off, r1=size, r2=ok(1/0), r3=0
X(L_RESET, 0x0223u, "L_RESET", 0) // reset worker local byte arena head to 0
X(L_SCOPE_ENTER, 0x0224u, "L_SCOPE_ENTER", 0) // push current local byte arena head onto scope stack
X(L_SCOPE_LEAVE, 0x0225u, "L_SCOPE_LEAVE", 0) // restore local byte arena head from scope stack
X(L_SCOPE_ALLOC, 0x0226u, "L_SCOPE_ALLOC", 0) // like L_ALLOC, intended for scoped allocations

X(FMT, 0x0745u, "FMT", 1) // u8 argc
X(LOG, 0x0746u, "LOG", 0) // consumes (r0=off, r1=len, r2=kind)


X(AT_PARALLEL, 0x0800u, "AT_PARALLEL", 0) // consumes (r0=EPA_FUNCTION, r1=GHS_IDX, r2=GHS_GEN, r3=THREAD_COUNT)


//
// ---------- COMMON: GPU semantic layer (higher = more abstract) ----------
//
// Resource lifecycle
X(CLEAR_RGBA_DEPTH_F32, 	 0x0F10u, "CLEAR",    20) // 5*f32
X(VIEWPORT_I32,         	 0x0F11u, "VIEWPORT", 16) // 4*i32
X(DRAW,                 	 0x0F12u, "DRAW",     12) // 3*u32

X(GPU_RES_DELETE,            0x1000u, "GPU_RES_DELETE",  8)   // u32 kind, u32 id

// Buffers
X(GPU_BUF_CREATE,            0x1010u, "GPU_BUF_CREATE",  16)  // u32 id,target,usage,size
X(GPU_BUF_BIND,              0x1011u, "GPU_BUF_BIND",     8)  // u32 target,id
X(GPU_BUF_BIND_BASE,         0x1012u, "GPU_BUF_BIND_BASE",12) // u32 target,index,id
X(GPU_BUF_SUBDATA,           0x1013u, "GPU_BUF_SUBDATA",  0xFF) // variable

// Vertex layout (portable “VAO”)
X(GPU_VTX_LAYOUT_CREATE,     0x1100u, "GPU_VTXLAY_CREATE", 4)  // u32 layout_id
X(GPU_VTX_LAYOUT_BIND,       0x1101u, "GPU_VTXLAY_BIND",   4)  // u32 layout_id
X(GPU_VTX_BIND_VBO,          0x1102u, "GPU_VTX_VBO",       8)  // u32 layout_id,vbo_id
X(GPU_VTX_BIND_EBO,          0x1103u, "GPU_VTX_EBO",       8)  // u32 layout_id,ebo_id
X(GPU_VTX_ATTRIB_FMT,        0x1104u, "GPU_VTX_ATTR_FMT", 28)  // 7*u32
X(GPU_VTX_ATTRIB_ENABLE,     0x1105u, "GPU_VTX_ATTR_EN",  12)  // u32 layout_id,attr_index,enable

// Textures + samplers
X(GPU_TEX_CREATE,            0x1200u, "GPU_TEX_CREATE",   28)  // 7*u32
X(GPU_TEX_BIND_UNIT,         0x1201u, "GPU_TEX_BIND",     12)  // u32 unit,tex_id,sampler_id
X(GPU_SAMPLER_CREATE,        0x1202u, "GPU_SAMP_CREATE",   4)  // u32 sampler_id
X(GPU_SAMPLER_PARAM,         0x1203u, "GPU_SAMP_PARAM",   16)  // u32 sampler_id,pname,v0,v1
X(GPU_TEX_SUBIMAGE,          0x1204u, "GPU_TEX_SUBIMG",   0xFF) // variable

// Shaders / programs
X(GPU_SHADER_LOAD_BLOB,      0x1300u, "GPU_SHD_LOAD",     0xFF) // variable
X(GPU_PROGRAM_LINK_VF,       0x1301u, "GPU_PROG_LINKVF",  12)   // u32 prog,vs,fs
X(GPU_PROGRAM_USE,           0x1302u, "GPU_PROG_USE",      4)   // u32 prog

// Render targets (portable “FBO”)
X(GPU_RT_CREATE,             0x1400u, "GPU_RT_CREATE",     4)   // u32 rt_id
X(GPU_RT_BIND,               0x1401u, "GPU_RT_BIND",       4)   // u32 rt_id (0=default)
X(GPU_RT_ATTACH_TEX,         0x1402u, "GPU_RT_ATTACH",    20)   // 5*u32
X(GPU_RT_BLIT,               0x1403u, "GPU_RT_BLIT",      48)   // see comment in header

// Fixed-function style state (semantic; CUDA may emulate)
X(GPU_SET_BLEND,             0x1500u, "GPU_SET_BLEND",    20)   // 5*u32
X(GPU_SET_DEPTH,             0x1501u, "GPU_SET_DEPTH",    12)   // 3*u32
X(GPU_SET_CULL,              0x1502u, "GPU_SET_CULL",     12)   // 3*u32
X(GPU_SET_SCISSOR,           0x1503u, "GPU_SET_SCISSOR",  20)   // u32 enable + 4*i32
X(GPU_SET_COLOR_MASK,        0x1504u, "GPU_SET_CMASK",    16)   // 4*u32
X(GPU_SET_DEPTH_BIAS,        0x1505u, "GPU_SET_DBias",     8)   // 2*f32

// Draw variants (semantic)
X(GPU_DRAW_INDEXED,          0x1600u, "GPU_DRAW_IDX",     20)   // 5*u32
X(GPU_DRAW_INSTANCED,        0x1601u, "GPU_DRAW_INST",    16)   // 4*u32
X(GPU_DRAW_INDEXED_INST,     0x1602u, "GPU_DRAW_IDXI",    24)   // 6*u32

// Compute + barriers
X(GPU_DISPATCH,              0x1700u, "GPU_DISPATCH",     12)   // 3*u32
X(GPU_MEMORY_BARRIER,        0x1701u, "GPU_BARRIER",       4)   // u32 bits

// Debug / profiling
X(GPU_DEBUG_LABEL,           0x1800u, "GPU_DBG_LABEL",    0xFF) // variable
X(GPU_QUERY_BEGIN,           0x1801u, "GPU_Q_BEGIN",       8)   // u32 query_id,kind
X(GPU_QUERY_END,             0x1802u, "GPU_Q_END",         4)   // u32 kind
X(GPU_QUERY_RESULT_U64,      0x1803u, "GPU_Q_RESULT64",    8)   // u32 query_id,dst

// Fences / sync (portable)
X(GPU_FENCE_INSERT,          0x1900u, "GPU_FENCE_INS",     4)   // u32 fence_id
X(GPU_FENCE_WAIT,            0x1901u, "GPU_FENCE_WAIT",   12)   // u32 fence_id,timeout_ms,flags
X(GPU_FENCE_DELETE,          0x1902u, "GPU_FENCE_DEL",     4)   // u32 fence_id

// Present (portable; backend may no-op if headless)
X(GPU_PRESENT,               0x1A00u, "GPU_PRESENT",       0)

// ---- GPU pipeline (common extras) ----
X(GPU_PROGRAM_LINK_COMP,     0x1B00u, "GPU_PROGRAM_LINK_COMP",  8)  // u32 prog_id, cs_id
X(GPU_TEX_GEN_MIPMAPS,       0x1B01u, "GPU_TEX_GEN_MIPMAPS",    4)  // u32 tex_id
X(GPU_BIND_IMAGE_TEX,        0x1B02u, "GPU_BIND_IMAGE_TEX",    24)  // 6*u32

// ---- Uniforms ----
X(GPU_UNIFORM_LOC,           0x1C00u, "GPU_UNIFORM_LOC",   0xFF) // variable (name bytes)
X(GPU_UNIFORM_1I,            0x1C01u, "GPU_UNIFORM_1I",     8)   // u32 loc, u32 v
X(GPU_UNIFORM_1F,            0x1C02u, "GPU_UNIFORM_1F",     8)   // u32 loc, f32 v
X(GPU_UNIFORM_4F,            0x1C03u, "GPU_UNIFORM_4F",    20)   // u32 loc + 4*f32
X(GPU_UNIFORM_MAT4F,         0x1C04u, "GPU_UNIFORM_MAT4F", 72)   // u32 loc,u32 transpose + 16*f32

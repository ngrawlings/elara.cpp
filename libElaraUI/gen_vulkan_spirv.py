#!/usr/bin/env python3
"""Generate SPIR-V compute shader for ElaraVulkanSurfaceWidget.

The command buffer has a 5-word header followed by command records:
  cmds[0] = pixel width  (int32 bits)
  cmds[1] = pixel height (int32 bits)
  cmds[2] = virtual_width  (float32 bits)
  cmds[3] = virtual_height (float32 bits)
  cmds[4] = command_count  (int32 bits)
  cmds[5..] = command records, each 10 uint32 words (40 bytes)
"""
import struct, sys

MAGIC = 0x07230203
VER   = 0x00010300  # SPIR-V 1.3

def fword(f): return struct.unpack('<I', struct.pack('<f', f))[0]

def strwords(s):
    b = s.encode('utf-8') + b'\x00'
    while len(b) % 4: b += b'\x00'
    return list(struct.unpack('<' + 'I'*(len(b)//4), b))

class S:
    def __init__(self):
        self.w = []
        self.nid = 1

    def ID(self):
        r = self.nid; self.nid += 1; return r

    def E(self, op, *args):
        self.w.append(((len(args)+1)<<16)|op)
        self.w.extend(args)

    def ES(self, op, *args, name):
        sw = strwords(name)
        self.w.append(((len(args)+len(sw)+1)<<16)|op)
        self.w.extend(args); self.w.extend(sw)

    def done(self):
        hdr = [MAGIC, VER, 0, self.nid, 0]
        data = hdr + self.w
        return struct.pack('<'+'I'*len(data), *data)

# Opcodes
OP = dict(
    Capability=17, ExtInstImport=11, MemoryModel=14, EntryPoint=15,
    ExecutionMode=16, Decorate=71, MemberDecorate=72,
    TypeVoid=19, TypeBool=20, TypeInt=21, TypeFloat=22, TypeVector=23,
    TypeRuntimeArray=29, TypeStruct=30, TypePointer=32, TypeFunction=33,
    Constant=43, Variable=59, AccessChain=65, Load=61, Store=62,
    Function=54, FunctionEnd=56, Label=248, Branch=249,
    BranchConditional=250, LoopMerge=246, SelectionMerge=247, Return=253,
    IAdd=128, ISub=130, IMul=132, IEqual=170, INotEqual=171,
    SLessThan=177, ULessThan=176,
    FAdd=129, FSub=131, FMul=133, FDiv=136,
    FOrdLessThan=184, FOrdGreaterThan=185,
    FOrdLessThanEqual=186, FOrdGreaterThanEqual=187,
    CompositeExtract=81, Bitcast=124,
    ConvertUToF=111, ConvertSToF=112, ConvertFToU=109,
    ShiftLeftLogical=196, BitwiseOr=197,
    LogicalAnd=167, LogicalOr=166,
    Select=169, ExtInst=12, Phi=245, Dot=148,
)
# GLSL.std.450
GLSL = dict(FMax=40, FMin=37, FClamp=43, Length=66, Sqrt=31)

def build():
    s = S()

    # --- Allocate IDs ---
    GLSL_EXT = s.ID()
    T_void = s.ID(); T_bool = s.ID(); T_int = s.ID(); T_uint = s.ID(); T_float = s.ID()
    T_v2f  = s.ID(); T_uvec3 = s.ID()
    T_ra_uint = s.ID()
    T_spx   = s.ID()   # struct PixelBuffer { uint[] }
    T_scmd  = s.ID()   # struct CmdBuffer   { uint[] }
    T_ptr_sb_spx  = s.ID()
    T_ptr_sb_scmd = s.ID()
    T_ptr_sb_uint = s.ID()
    T_ptr_in_uv3  = s.ID()
    T_fn_void     = s.ID()

    V_pixels  = s.ID()
    V_cmds    = s.ID()
    V_glob_id = s.ID()

    # int constants (CI5 = header size, CI12 = cmd offset from base)
    CI0=s.ID(); CI1=s.ID(); CI2=s.ID()
    CI3=s.ID(); CI4=s.ID(); CI5=s.ID()
    CI7=s.ID(); CI8=s.ID(); CI9=s.ID(); CI10=s.ID()
    # uint constants
    CU8=s.ID(); CU16=s.ID(); CU_ALPHA=s.ID()  # 0xFF000000
    # float constants
    CF0=s.ID(); CF1=s.ID(); CF255=s.ID()
    CF010=s.ID(); CF011=s.ID(); CF014=s.ID(); CF15=s.ID()

    FN_main = s.ID()

    # --- Labels (pre-allocate) ---
    L_entry        = s.ID()
    L_bounds_merge = s.ID()
    L_loop_hdr     = s.ID()
    L_loop_body    = s.ID()
    L_loop_cont    = s.ID()
    L_loop_merge   = s.ID()
    L_op0_true     = s.ID()
    L_op0_merge    = s.ID()
    L_op1_true     = s.ID()
    L_op1_merge    = s.ID()
    L_op2_true     = s.ID()
    L_op2_merge    = s.ID()
    L_rect_inner   = s.ID()
    L_rect_merge   = s.ID()
    L_line_inner   = s.ID()
    L_line_merge   = s.ID()

    # Extra temp IDs for function body
    def tid(): return s.ID()

    # === Capabilities & memory model ===
    s.E(OP['Capability'], 1)           # Shader
    s.ES(OP['ExtInstImport'], GLSL_EXT, name="GLSL.std.450")
    s.E(OP['MemoryModel'], 0, 1)       # Logical, GLSL450
    # EntryPoint: GLCompute(5), fn, "main", interface vars
    s.E(OP['EntryPoint'], 5, FN_main, *strwords("main"), V_glob_id)
    s.E(OP['ExecutionMode'], FN_main, 17, 16, 16, 1)  # LocalSize 16 16 1

    # === Decorations ===
    s.E(OP['Decorate'], T_ra_uint, 6, 4)          # ArrayStride 4
    s.E(OP['Decorate'], T_spx, 2)                 # Block
    s.E(OP['MemberDecorate'], T_spx, 0, 35, 0)    # Offset 0
    s.E(OP['Decorate'], V_pixels, 34, 0)           # DescriptorSet 0
    s.E(OP['Decorate'], V_pixels, 33, 0)           # Binding 0

    s.E(OP['Decorate'], T_scmd, 2)                 # Block
    s.E(OP['MemberDecorate'], T_scmd, 0, 35, 0)    # Offset 0
    s.E(OP['Decorate'], V_cmds, 34, 0)             # DescriptorSet 0
    s.E(OP['Decorate'], V_cmds, 33, 1)             # Binding 1
    s.E(OP['MemberDecorate'], T_scmd, 0, 24)       # NonWritable

    s.E(OP['Decorate'], V_glob_id, 11, 28)         # BuiltIn GlobalInvocationId

    # === Types ===
    s.E(OP['TypeVoid'],   T_void)
    s.E(OP['TypeBool'],   T_bool)
    s.E(OP['TypeInt'],    T_int,   32, 1)           # i32 signed
    s.E(OP['TypeInt'],    T_uint,  32, 0)           # u32 unsigned
    s.E(OP['TypeFloat'],  T_float, 32)
    s.E(OP['TypeVector'], T_v2f,   T_float, 2)
    s.E(OP['TypeVector'], T_uvec3, T_uint, 3)
    s.E(OP['TypeRuntimeArray'], T_ra_uint, T_uint)
    s.E(OP['TypeStruct'], T_spx, T_ra_uint)
    s.E(OP['TypeStruct'], T_scmd, T_ra_uint)
    s.E(OP['TypePointer'], T_ptr_sb_spx,  12, T_spx)
    s.E(OP['TypePointer'], T_ptr_sb_scmd, 12, T_scmd)
    s.E(OP['TypePointer'], T_ptr_sb_uint, 12, T_uint)
    s.E(OP['TypePointer'], T_ptr_in_uv3,   1, T_uvec3)
    s.E(OP['TypeFunction'], T_fn_void, T_void)

    # === Variables ===
    s.E(OP['Variable'], T_ptr_sb_spx,  V_pixels,  12)
    s.E(OP['Variable'], T_ptr_sb_scmd, V_cmds,    12)
    s.E(OP['Variable'], T_ptr_in_uv3,  V_glob_id,  1)

    # === Constants ===
    s.E(OP['Constant'], T_int,  CI0,  0)
    s.E(OP['Constant'], T_int,  CI1,  1)
    s.E(OP['Constant'], T_int,  CI2,  2)
    s.E(OP['Constant'], T_int,  CI3,  3)
    s.E(OP['Constant'], T_int,  CI4,  4)
    s.E(OP['Constant'], T_int,  CI5,  5)
    s.E(OP['Constant'], T_int,  CI7,  7)
    s.E(OP['Constant'], T_int,  CI8,  8)
    s.E(OP['Constant'], T_int,  CI9,  9)
    s.E(OP['Constant'], T_int,  CI10, 10)
    s.E(OP['Constant'], T_uint, CU8,  8)
    s.E(OP['Constant'], T_uint, CU16, 16)
    s.E(OP['Constant'], T_uint, CU_ALPHA, 0xFF000000)
    s.E(OP['Constant'], T_float, CF0,   fword(0.0))
    s.E(OP['Constant'], T_float, CF1,   fword(1.0))
    s.E(OP['Constant'], T_float, CF255, fword(255.0))
    s.E(OP['Constant'], T_float, CF010, fword(0.10))
    s.E(OP['Constant'], T_float, CF011, fword(0.11))
    s.E(OP['Constant'], T_float, CF014, fword(0.14))
    s.E(OP['Constant'], T_float, CF15,  fword(1.5))

    # === Function: main ===
    s.E(OP['Function'], T_void, FN_main, 0, T_fn_void)
    s.E(OP['Label'], L_entry)

    # Load gx, gy from global invocation ID
    gid_val = tid()
    s.E(OP['Load'], T_uvec3, gid_val, V_glob_id)
    gx_u = tid()
    s.E(OP['CompositeExtract'], T_uint, gx_u, gid_val, 0)
    gy_u = tid()
    s.E(OP['CompositeExtract'], T_uint, gy_u, gid_val, 1)

    # Helper: load uint32 from cmds[idx_const] as int (via bitcast)
    def load_hdr_int(idx_const):
        idx_u = tid()
        s.E(OP['Bitcast'], T_uint, idx_u, idx_const)
        ptr = tid()
        s.E(OP['AccessChain'], T_ptr_sb_uint, ptr, V_cmds, CI0, idx_u)
        u = tid()
        s.E(OP['Load'], T_uint, u, ptr)
        i = tid()
        s.E(OP['Bitcast'], T_int, i, u)
        return i

    # Helper: load uint32 from cmds[idx_const] as float (via bitcast)
    def load_hdr_float(idx_const):
        idx_u = tid()
        s.E(OP['Bitcast'], T_uint, idx_u, idx_const)
        ptr = tid()
        s.E(OP['AccessChain'], T_ptr_sb_uint, ptr, V_cmds, CI0, idx_u)
        u = tid()
        s.E(OP['Load'], T_uint, u, ptr)
        f = tid()
        s.E(OP['Bitcast'], T_float, f, u)
        return f

    # Load width, height, virtual_width, virtual_height, command_count from header
    pc_width  = load_hdr_int(CI0)    # cmds[0] = pixel width
    pc_height = load_hdr_int(CI1)    # cmds[1] = pixel height
    vw        = load_hdr_float(CI2)  # cmds[2] = virtual_width
    vh        = load_hdr_float(CI3)  # cmds[3] = virtual_height
    cc        = load_hdr_int(CI4)    # cmds[4] = command_count

    # Convert width/height to uint for bounds comparison
    pc_width_u = tid()
    s.E(OP['Bitcast'], T_uint, pc_width_u, pc_width)
    pc_height_u = tid()
    s.E(OP['Bitcast'], T_uint, pc_height_u, pc_height)

    # Bounds check: gx >= width || gy >= height → early return
    gx_oob = tid()
    s.E(OP['ULessThan'], T_bool, gx_oob, gx_u, pc_width_u)   # gx < width
    gy_oob = tid()
    s.E(OP['ULessThan'], T_bool, gy_oob, gy_u, pc_height_u)  # gy < height
    in_bounds = tid()
    s.E(OP['LogicalAnd'], T_bool, in_bounds, gx_oob, gy_oob)

    L_early_ret = s.ID()
    s.E(OP['SelectionMerge'], L_bounds_merge, 0)
    s.E(OP['BranchConditional'], in_bounds, L_bounds_merge, L_early_ret)

    s.E(OP['Label'], L_early_ret)
    s.E(OP['Return'])

    s.E(OP['Label'], L_bounds_merge)

    # Convert gx, gy to float
    gx_f = tid()
    s.E(OP['ConvertUToF'], T_float, gx_f, gx_u)
    gy_f = tid()
    s.E(OP['ConvertUToF'], T_float, gy_f, gy_u)
    pw_f = tid()
    s.E(OP['ConvertSToF'], T_float, pw_f, pc_width)
    ph_f = tid()
    s.E(OP['ConvertSToF'], T_float, ph_f, pc_height)

    # vx = (gx_f / pw_f) * vw
    vx = tid()
    t1 = tid()
    s.E(OP['FDiv'], T_float, t1, gx_f, pw_f)
    s.E(OP['FMul'], T_float, vx, t1, vw)
    # vy = (gy_f / ph_f) * vh
    vy = tid()
    t2 = tid()
    s.E(OP['FDiv'], T_float, t2, gy_f, ph_f)
    s.E(OP['FMul'], T_float, vy, t2, vh)

    # line_width = max(1.5, vw / pw_f)
    lw_ratio = tid()
    s.E(OP['FDiv'], T_float, lw_ratio, vw, pw_f)
    line_width = tid()
    s.E(OP['ExtInst'], T_float, line_width, GLSL_EXT, GLSL['FMax'], lw_ratio, CF15)

    # === Loop over commands ===
    # phi: i (int), cr, cg, cb (float)
    # initial: i=0, cr=0.10, cg=0.11, cb=0.14
    s.E(OP['Branch'], L_loop_hdr)
    s.E(OP['Label'], L_loop_hdr)

    phi_i = tid()
    phi_i_pos = len(s.w)
    s.E(OP['Phi'], T_int,   phi_i,  CI0,   L_bounds_merge, 0, L_loop_cont)
    phi_cr = tid()
    phi_cr_pos = len(s.w)
    s.E(OP['Phi'], T_float, phi_cr, CF010, L_bounds_merge, 0, L_loop_cont)
    phi_cg = tid()
    phi_cg_pos = len(s.w)
    s.E(OP['Phi'], T_float, phi_cg, CF011, L_bounds_merge, 0, L_loop_cont)
    phi_cb = tid()
    phi_cb_pos = len(s.w)
    s.E(OP['Phi'], T_float, phi_cb, CF014, L_bounds_merge, 0, L_loop_cont)

    loop_cond = tid()
    s.E(OP['SLessThan'], T_bool, loop_cond, phi_i, cc)
    s.E(OP['LoopMerge'], L_loop_merge, L_loop_cont, 0)
    s.E(OP['BranchConditional'], loop_cond, L_loop_body, L_loop_merge)

    # --- Loop body ---
    s.E(OP['Label'], L_loop_body)

    # base = CI5 + i * 10  (skip the 5-word header)
    base_mul = tid()
    s.E(OP['IMul'], T_int, base_mul, phi_i, CI10)
    base = tid()
    s.E(OP['IAdd'], T_int, base, base_mul, CI5)

    # Load op (uint, cast to int)
    idx_op = tid()
    s.E(OP['Bitcast'], T_uint, idx_op, base)
    ptr_op = tid()
    s.E(OP['AccessChain'], T_ptr_sb_uint, ptr_op, V_cmds, CI0, idx_op)
    op_u = tid()
    s.E(OP['Load'], T_uint, op_u, ptr_op)
    op_i = tid()
    s.E(OP['Bitcast'], T_int, op_i, op_u)

    def load_float_at(offset_ci):
        idx = tid()
        off = tid()
        s.E(OP['IAdd'], T_int, off, base, offset_ci)
        s.E(OP['Bitcast'], T_uint, idx, off)
        p = tid()
        s.E(OP['AccessChain'], T_ptr_sb_uint, p, V_cmds, CI0, idx)
        u = tid()
        s.E(OP['Load'], T_uint, u, p)
        f = tid()
        s.E(OP['Bitcast'], T_float, f, u)
        return f

    x0 = load_float_at(CI1)
    y0 = load_float_at(CI2)
    x1 = load_float_at(CI3)
    y1 = load_float_at(CI4)
    cmd_r = load_float_at(CI7)
    cmd_g = load_float_at(CI8)
    cmd_b = load_float_at(CI9)

    # --- if op == 0 (clear) ---
    cond_op0 = tid()
    s.E(OP['IEqual'], T_bool, cond_op0, op_i, CI0)
    s.E(OP['SelectionMerge'], L_op0_merge, 0)
    s.E(OP['BranchConditional'], cond_op0, L_op0_true, L_op0_merge)

    s.E(OP['Label'], L_op0_true)
    s.E(OP['Branch'], L_op0_merge)

    s.E(OP['Label'], L_op0_merge)
    # After op0: cr/cg/cb = cmd_r/g/b if op==0, else phi_cr/g/b
    cr_after0 = tid()
    cg_after0 = tid()
    cb_after0 = tid()
    s.E(OP['Phi'], T_float, cr_after0, cmd_r, L_op0_true, phi_cr, L_loop_body)
    s.E(OP['Phi'], T_float, cg_after0, cmd_g, L_op0_true, phi_cg, L_loop_body)
    s.E(OP['Phi'], T_float, cb_after0, cmd_b, L_op0_true, phi_cb, L_loop_body)

    # --- if op == 1 (rect) ---
    cond_op1 = tid()
    s.E(OP['IEqual'], T_bool, cond_op1, op_i, CI1)
    s.E(OP['SelectionMerge'], L_op1_merge, 0)
    s.E(OP['BranchConditional'], cond_op1, L_op1_true, L_op1_merge)

    s.E(OP['Label'], L_op1_true)
    # Check: vx >= x0 && vx < x0+x1 && vy >= y0 && vy < y0+y1
    x0px1 = tid(); y0py1 = tid()
    s.E(OP['FAdd'], T_float, x0px1, x0, x1)
    s.E(OP['FAdd'], T_float, y0py1, y0, y1)
    c1 = tid(); c2 = tid(); c3 = tid(); c4 = tid()
    s.E(OP['FOrdGreaterThanEqual'], T_bool, c1, vx, x0)
    s.E(OP['FOrdLessThan'],         T_bool, c2, vx, x0px1)
    s.E(OP['FOrdGreaterThanEqual'], T_bool, c3, vy, y0)
    s.E(OP['FOrdLessThan'],         T_bool, c4, vy, y0py1)
    ca = tid(); cb_cond = tid()
    s.E(OP['LogicalAnd'], T_bool, ca, c1, c2)
    s.E(OP['LogicalAnd'], T_bool, cb_cond, c3, c4)
    rect_hit = tid()
    s.E(OP['LogicalAnd'], T_bool, rect_hit, ca, cb_cond)
    s.E(OP['SelectionMerge'], L_rect_merge, 0)
    s.E(OP['BranchConditional'], rect_hit, L_rect_inner, L_rect_merge)

    s.E(OP['Label'], L_rect_inner)
    s.E(OP['Branch'], L_rect_merge)

    s.E(OP['Label'], L_rect_merge)
    cr_rect = tid(); cg_rect = tid(); cb_rect = tid()
    s.E(OP['Phi'], T_float, cr_rect, cmd_r, L_rect_inner, cr_after0, L_op1_true)
    s.E(OP['Phi'], T_float, cg_rect, cmd_g, L_rect_inner, cg_after0, L_op1_true)
    s.E(OP['Phi'], T_float, cb_rect, cmd_b, L_rect_inner, cb_after0, L_op1_true)
    s.E(OP['Branch'], L_op1_merge)

    s.E(OP['Label'], L_op1_merge)
    cr_after1 = tid(); cg_after1 = tid(); cb_after1 = tid()
    s.E(OP['Phi'], T_float, cr_after1, cr_rect, L_rect_merge, cr_after0, L_op0_merge)
    s.E(OP['Phi'], T_float, cg_after1, cg_rect, L_rect_merge, cg_after0, L_op0_merge)
    s.E(OP['Phi'], T_float, cb_after1, cb_rect, L_rect_merge, cb_after0, L_op0_merge)

    # --- if op == 2 (line) ---
    cond_op2 = tid()
    s.E(OP['IEqual'], T_bool, cond_op2, op_i, CI2)
    s.E(OP['SelectionMerge'], L_op2_merge, 0)
    s.E(OP['BranchConditional'], cond_op2, L_op2_true, L_op2_merge)

    s.E(OP['Label'], L_op2_true)
    # Inline lineDistance: dist between (vx,vy) and segment (x0,y0)-(x1,y1)
    # ab = (x1-x0, y1-y0)  [x1,y1 in cmd are endpoint, not width/height for lines]
    abx = tid(); aby = tid()
    s.E(OP['FSub'], T_float, abx, x1, x0)
    s.E(OP['FSub'], T_float, aby, y1, y0)
    # len2 = dot(ab, ab)
    len2_a = tid(); len2_b = tid(); len2 = tid()
    s.E(OP['FMul'], T_float, len2_a, abx, abx)
    s.E(OP['FMul'], T_float, len2_b, aby, aby)
    s.E(OP['FAdd'], T_float, len2, len2_a, len2_b)
    # dot(p-a, ab)
    pax = tid(); pay = tid()
    s.E(OP['FSub'], T_float, pax, vx, x0)
    s.E(OP['FSub'], T_float, pay, vy, y0)
    dot_pa_ab_x = tid(); dot_pa_ab_y = tid(); dot_pa_ab = tid()
    s.E(OP['FMul'], T_float, dot_pa_ab_x, pax, abx)
    s.E(OP['FMul'], T_float, dot_pa_ab_y, pay, aby)
    s.E(OP['FAdd'], T_float, dot_pa_ab, dot_pa_ab_x, dot_pa_ab_y)
    # t = clamp(dot_pa_ab / len2, 0, 1) if len2 > 0 else 0
    t_raw = tid()
    s.E(OP['FDiv'], T_float, t_raw, dot_pa_ab, len2)
    t_clamp = tid()
    s.E(OP['ExtInst'], T_float, t_clamp, GLSL_EXT, GLSL['FClamp'], t_raw, CF0, CF1)
    # proj = a + ab*t
    proj_x_off = tid(); proj_y_off = tid(); proj_x = tid(); proj_y = tid()
    s.E(OP['FMul'], T_float, proj_x_off, abx, t_clamp)
    s.E(OP['FMul'], T_float, proj_y_off, aby, t_clamp)
    s.E(OP['FAdd'], T_float, proj_x, x0, proj_x_off)
    s.E(OP['FAdd'], T_float, proj_y, y0, proj_y_off)
    # dist = length(p - proj)
    dx = tid(); dy = tid(); dx2 = tid(); dy2 = tid(); dist2 = tid(); dist = tid()
    s.E(OP['FSub'], T_float, dx, vx, proj_x)
    s.E(OP['FSub'], T_float, dy, vy, proj_y)
    s.E(OP['FMul'], T_float, dx2, dx, dx)
    s.E(OP['FMul'], T_float, dy2, dy, dy)
    s.E(OP['FAdd'], T_float, dist2, dx2, dy2)
    s.E(OP['ExtInst'], T_float, dist, GLSL_EXT, GLSL['Sqrt'], dist2)
    # if dist <= line_width: hit
    line_hit = tid()
    s.E(OP['FOrdLessThanEqual'], T_bool, line_hit, dist, line_width)
    s.E(OP['SelectionMerge'], L_line_merge, 0)
    s.E(OP['BranchConditional'], line_hit, L_line_inner, L_line_merge)

    s.E(OP['Label'], L_line_inner)
    s.E(OP['Branch'], L_line_merge)

    s.E(OP['Label'], L_line_merge)
    cr_line = tid(); cg_line = tid(); cb_line = tid()
    s.E(OP['Phi'], T_float, cr_line, cmd_r, L_line_inner, cr_after1, L_op2_true)
    s.E(OP['Phi'], T_float, cg_line, cmd_g, L_line_inner, cg_after1, L_op2_true)
    s.E(OP['Phi'], T_float, cb_line, cmd_b, L_line_inner, cb_after1, L_op2_true)
    s.E(OP['Branch'], L_op2_merge)

    s.E(OP['Label'], L_op2_merge)
    cr_new = tid(); cg_new = tid(); cb_new = tid()
    s.E(OP['Phi'], T_float, cr_new, cr_line, L_line_merge, cr_after1, L_op1_merge)
    s.E(OP['Phi'], T_float, cg_new, cg_line, L_line_merge, cg_after1, L_op1_merge)
    s.E(OP['Phi'], T_float, cb_new, cb_line, L_line_merge, cb_after1, L_op1_merge)

    s.E(OP['Branch'], L_loop_cont)

    # --- Continue block: i++ ---
    s.E(OP['Label'], L_loop_cont)
    i_next = tid()
    s.E(OP['IAdd'], T_int, i_next, phi_i, CI1)
    # Patch phi values for continue block
    s.w[phi_i_pos  + 5] = i_next
    s.w[phi_i_pos  + 6] = L_loop_cont
    s.w[phi_cr_pos + 5] = cr_new
    s.w[phi_cr_pos + 6] = L_loop_cont
    s.w[phi_cg_pos + 5] = cg_new
    s.w[phi_cg_pos + 6] = L_loop_cont
    s.w[phi_cb_pos + 5] = cb_new
    s.w[phi_cb_pos + 6] = L_loop_cont
    s.E(OP['Branch'], L_loop_hdr)

    # --- Loop merge: write pixel ---
    s.E(OP['Label'], L_loop_merge)

    # clamp and convert to uint
    cr_c = tid(); cg_c = tid(); cb_c = tid()
    s.E(OP['ExtInst'], T_float, cr_c, GLSL_EXT, GLSL['FClamp'], phi_cr, CF0, CF1)
    s.E(OP['ExtInst'], T_float, cg_c, GLSL_EXT, GLSL['FClamp'], phi_cg, CF0, CF1)
    s.E(OP['ExtInst'], T_float, cb_c, GLSL_EXT, GLSL['FClamp'], phi_cb, CF0, CF1)
    cr_255 = tid(); cg_255 = tid(); cb_255 = tid()
    s.E(OP['FMul'], T_float, cr_255, cr_c, CF255)
    s.E(OP['FMul'], T_float, cg_255, cg_c, CF255)
    s.E(OP['FMul'], T_float, cb_255, cb_c, CF255)
    ru = tid(); gu = tid(); bu = tid()
    s.E(OP['ConvertFToU'], T_uint, ru, cr_255)
    s.E(OP['ConvertFToU'], T_uint, gu, cg_255)
    s.E(OP['ConvertFToU'], T_uint, bu, cb_255)
    g8 = tid(); b16 = tid()
    s.E(OP['ShiftLeftLogical'], T_uint, g8,  gu, CU8)
    s.E(OP['ShiftLeftLogical'], T_uint, b16, bu, CU16)
    pix0 = tid(); pix1 = tid(); pixel = tid()
    s.E(OP['BitwiseOr'], T_uint, pix0, ru,   g8)
    s.E(OP['BitwiseOr'], T_uint, pix1, pix0, b16)
    s.E(OP['BitwiseOr'], T_uint, pixel, pix1, CU_ALPHA)

    # index = gy * width + gx
    gy_i = tid()
    s.E(OP['Bitcast'], T_int, gy_i, gy_u)
    idx_row = tid()
    s.E(OP['IMul'], T_int, idx_row, gy_i, pc_width)
    gx_i = tid()
    s.E(OP['Bitcast'], T_int, gx_i, gx_u)
    idx_flat = tid()
    s.E(OP['IAdd'], T_int, idx_flat, idx_row, gx_i)
    idx_u = tid()
    s.E(OP['Bitcast'], T_uint, idx_u, idx_flat)

    ptr_pix = tid()
    s.E(OP['AccessChain'], T_ptr_sb_uint, ptr_pix, V_pixels, CI0, idx_u)
    s.E(OP['Store'], ptr_pix, pixel)

    s.E(OP['Return'])
    s.E(OP['FunctionEnd'])

    return s.done()

data = build()

# Emit as C++ uint32_t array
words = struct.unpack('<' + 'I'*(len(data)//4), data)
print('static const uint32_t kVulkanSurfaceSPIRV[] = {')
line = '   '
for i, w in enumerate(words):
    line += ' 0x{:08x}u,'.format(w)
    if (i+1) % 8 == 0:
        print(line)
        line = '   '
if line.strip():
    print(line)
print('};')
print('static const size_t kVulkanSurfaceSPIRVSize = sizeof(kVulkanSurfaceSPIRV);')

#!/usr/bin/env python3
"""Generate raw spirv.dat for Orange Fortress Vulkan surface rendering.

This is vendored into the Orange Fortress project so destination-machine shader
compilation does not depend on libElaraUI source files being present.
"""

from __future__ import annotations

import pathlib
import struct
import sys

MAGIC = 0x07230203
VER = 0x00010300  # SPIR-V 1.3


def fword(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def strwords(value: str) -> list[int]:
    raw = value.encode("utf-8") + b"\x00"
    while len(raw) % 4:
        raw += b"\x00"
    return list(struct.unpack("<" + "I" * (len(raw) // 4), raw))


class SpirvBuilder:
    def __init__(self) -> None:
        self.words: list[int] = []
        self.next_id = 1

    def alloc_id(self) -> int:
        result = self.next_id
        self.next_id += 1
        return result

    def emit(self, opcode: int, *args: int) -> None:
        self.words.append(((len(args) + 1) << 16) | opcode)
        self.words.extend(args)

    def emit_string(self, opcode: int, *args: int, name: str) -> None:
        sw = strwords(name)
        self.words.append(((len(args) + len(sw) + 1) << 16) | opcode)
        self.words.extend(args)
        self.words.extend(sw)

    def done(self) -> bytes:
        header = [MAGIC, VER, 0, self.next_id, 0]
        data = header + self.words
        return struct.pack("<" + "I" * len(data), *data)


OP = dict(
    Capability=17,
    ExtInstImport=11,
    MemoryModel=14,
    EntryPoint=15,
    ExecutionMode=16,
    Decorate=71,
    MemberDecorate=72,
    TypeVoid=19,
    TypeBool=20,
    TypeInt=21,
    TypeFloat=22,
    TypeVector=23,
    TypeRuntimeArray=29,
    TypeStruct=30,
    TypePointer=32,
    TypeFunction=33,
    Constant=43,
    Variable=59,
    AccessChain=65,
    Load=61,
    Store=62,
    Function=54,
    FunctionEnd=56,
    Label=248,
    Branch=249,
    BranchConditional=250,
    LoopMerge=246,
    SelectionMerge=247,
    Return=253,
    IAdd=128,
    ISub=130,
    IMul=132,
    IEqual=170,
    INotEqual=171,
    SLessThan=177,
    ULessThan=176,
    FAdd=129,
    FSub=131,
    FMul=133,
    FDiv=136,
    FOrdLessThan=184,
    FOrdGreaterThan=185,
    FOrdLessThanEqual=186,
    FOrdGreaterThanEqual=187,
    CompositeExtract=81,
    Bitcast=124,
    ConvertUToF=111,
    ConvertSToF=112,
    ConvertFToU=109,
    ShiftLeftLogical=196,
    BitwiseOr=197,
    LogicalAnd=167,
    LogicalOr=166,
    Select=169,
    ExtInst=12,
    Phi=245,
    Dot=148,
)

GLSL = dict(FMax=40, FMin=37, FClamp=43, Length=66, Sqrt=31)


def build() -> bytes:
    s = SpirvBuilder()

    glsl_ext = s.alloc_id()
    t_void = s.alloc_id()
    t_bool = s.alloc_id()
    t_int = s.alloc_id()
    t_uint = s.alloc_id()
    t_float = s.alloc_id()
    t_v2f = s.alloc_id()
    t_uvec3 = s.alloc_id()
    t_ra_uint = s.alloc_id()
    t_spx = s.alloc_id()
    t_scmd = s.alloc_id()
    t_stex = s.alloc_id()
    t_ptr_sb_spx = s.alloc_id()
    t_ptr_sb_scmd = s.alloc_id()
    t_ptr_sb_stex = s.alloc_id()
    t_ptr_sb_uint = s.alloc_id()
    t_ptr_in_uv3 = s.alloc_id()
    t_fn_void = s.alloc_id()

    v_pixels = s.alloc_id()
    v_cmds = s.alloc_id()
    v_tex = s.alloc_id()
    v_glob_id = s.alloc_id()

    ci0 = s.alloc_id()
    ci1 = s.alloc_id()
    ci2 = s.alloc_id()
    ci3 = s.alloc_id()
    ci4 = s.alloc_id()
    ci5 = s.alloc_id()
    ci6 = s.alloc_id()
    ci7 = s.alloc_id()
    ci8 = s.alloc_id()
    ci9 = s.alloc_id()
    ci10 = s.alloc_id()
    ci11 = s.alloc_id()
    ci12 = s.alloc_id()
    cu8 = s.alloc_id()
    cu16 = s.alloc_id()
    cu_alpha = s.alloc_id()
    cf0 = s.alloc_id()
    cf1 = s.alloc_id()
    cf255 = s.alloc_id()
    cf010 = s.alloc_id()
    cf011 = s.alloc_id()
    cf014 = s.alloc_id()
    cf15 = s.alloc_id()
    cf0999 = s.alloc_id()

    fn_main = s.alloc_id()

    l_entry = s.alloc_id()
    l_bounds_merge = s.alloc_id()
    l_loop_hdr = s.alloc_id()
    l_loop_body = s.alloc_id()
    l_loop_cont = s.alloc_id()
    l_loop_merge = s.alloc_id()
    l_op0_true = s.alloc_id()
    l_op0_merge = s.alloc_id()
    l_op1_true = s.alloc_id()
    l_op1_merge = s.alloc_id()
    l_op2_true = s.alloc_id()
    l_op2_merge = s.alloc_id()
    l_op3_true = s.alloc_id()
    l_op3_merge = s.alloc_id()
    l_op4_true = s.alloc_id()
    l_op4_merge = s.alloc_id()
    l_rect_inner = s.alloc_id()
    l_rect_merge = s.alloc_id()
    l_line_inner = s.alloc_id()
    l_line_merge = s.alloc_id()
    l_tri_inner = s.alloc_id()
    l_tri_merge = s.alloc_id()
    l_tex_inner = s.alloc_id()
    l_tex_merge = s.alloc_id()

    def tid() -> int:
        return s.alloc_id()

    s.emit(OP["Capability"], 1)
    s.emit_string(OP["ExtInstImport"], glsl_ext, name="GLSL.std.450")
    s.emit(OP["MemoryModel"], 0, 1)
    s.emit(OP["EntryPoint"], 5, fn_main, *strwords("main"), v_glob_id)
    s.emit(OP["ExecutionMode"], fn_main, 17, 16, 16, 1)

    s.emit(OP["Decorate"], t_ra_uint, 6, 4)
    s.emit(OP["Decorate"], t_spx, 2)
    s.emit(OP["MemberDecorate"], t_spx, 0, 35, 0)
    s.emit(OP["Decorate"], v_pixels, 34, 0)
    s.emit(OP["Decorate"], v_pixels, 33, 0)
    s.emit(OP["Decorate"], t_scmd, 2)
    s.emit(OP["MemberDecorate"], t_scmd, 0, 35, 0)
    s.emit(OP["Decorate"], v_cmds, 34, 0)
    s.emit(OP["Decorate"], v_cmds, 33, 1)
    s.emit(OP["MemberDecorate"], t_scmd, 0, 24)
    s.emit(OP["Decorate"], t_stex, 2)
    s.emit(OP["MemberDecorate"], t_stex, 0, 35, 0)
    s.emit(OP["Decorate"], v_tex, 34, 0)
    s.emit(OP["Decorate"], v_tex, 33, 2)
    s.emit(OP["MemberDecorate"], t_stex, 0, 24)
    s.emit(OP["Decorate"], v_glob_id, 11, 28)

    s.emit(OP["TypeVoid"], t_void)
    s.emit(OP["TypeBool"], t_bool)
    s.emit(OP["TypeInt"], t_int, 32, 1)
    s.emit(OP["TypeInt"], t_uint, 32, 0)
    s.emit(OP["TypeFloat"], t_float, 32)
    s.emit(OP["TypeVector"], t_v2f, t_float, 2)
    s.emit(OP["TypeVector"], t_uvec3, t_uint, 3)
    s.emit(OP["TypeRuntimeArray"], t_ra_uint, t_uint)
    s.emit(OP["TypeStruct"], t_spx, t_ra_uint)
    s.emit(OP["TypeStruct"], t_scmd, t_ra_uint)
    s.emit(OP["TypeStruct"], t_stex, t_ra_uint)
    s.emit(OP["TypePointer"], t_ptr_sb_spx, 12, t_spx)
    s.emit(OP["TypePointer"], t_ptr_sb_scmd, 12, t_scmd)
    s.emit(OP["TypePointer"], t_ptr_sb_stex, 12, t_stex)
    s.emit(OP["TypePointer"], t_ptr_sb_uint, 12, t_uint)
    s.emit(OP["TypePointer"], t_ptr_in_uv3, 1, t_uvec3)
    s.emit(OP["TypeFunction"], t_fn_void, t_void)

    s.emit(OP["Variable"], t_ptr_sb_spx, v_pixels, 12)
    s.emit(OP["Variable"], t_ptr_sb_scmd, v_cmds, 12)
    s.emit(OP["Variable"], t_ptr_sb_stex, v_tex, 12)
    s.emit(OP["Variable"], t_ptr_in_uv3, v_glob_id, 1)

    s.emit(OP["Constant"], t_int, ci0, 0)
    s.emit(OP["Constant"], t_int, ci1, 1)
    s.emit(OP["Constant"], t_int, ci2, 2)
    s.emit(OP["Constant"], t_int, ci3, 3)
    s.emit(OP["Constant"], t_int, ci4, 4)
    s.emit(OP["Constant"], t_int, ci5, 5)
    s.emit(OP["Constant"], t_int, ci6, 6)
    s.emit(OP["Constant"], t_int, ci7, 7)
    s.emit(OP["Constant"], t_int, ci8, 8)
    s.emit(OP["Constant"], t_int, ci9, 9)
    s.emit(OP["Constant"], t_int, ci10, 10)
    s.emit(OP["Constant"], t_int, ci11, 11)
    s.emit(OP["Constant"], t_int, ci12, 12)
    s.emit(OP["Constant"], t_uint, cu8, 8)
    s.emit(OP["Constant"], t_uint, cu16, 16)
    s.emit(OP["Constant"], t_uint, cu_alpha, 0xFF000000)
    s.emit(OP["Constant"], t_float, cf0, fword(0.0))
    s.emit(OP["Constant"], t_float, cf1, fword(1.0))
    s.emit(OP["Constant"], t_float, cf255, fword(255.0))
    s.emit(OP["Constant"], t_float, cf010, fword(0.10))
    s.emit(OP["Constant"], t_float, cf011, fword(0.11))
    s.emit(OP["Constant"], t_float, cf014, fword(0.14))
    s.emit(OP["Constant"], t_float, cf15, fword(1.5))
    s.emit(OP["Constant"], t_float, cf0999, fword(0.999))

    s.emit(OP["Function"], t_void, fn_main, 0, t_fn_void)
    s.emit(OP["Label"], l_entry)

    gid_val = tid()
    s.emit(OP["Load"], t_uvec3, gid_val, v_glob_id)
    gx_u = tid()
    s.emit(OP["CompositeExtract"], t_uint, gx_u, gid_val, 0)
    gy_u = tid()
    s.emit(OP["CompositeExtract"], t_uint, gy_u, gid_val, 1)

    def load_hdr_int(idx_const: int) -> int:
        idx_u = tid()
        s.emit(OP["Bitcast"], t_uint, idx_u, idx_const)
        ptr = tid()
        s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr, v_cmds, ci0, idx_u)
        u = tid()
        s.emit(OP["Load"], t_uint, u, ptr)
        i = tid()
        s.emit(OP["Bitcast"], t_int, i, u)
        return i

    def load_hdr_float(idx_const: int) -> int:
        idx_u = tid()
        s.emit(OP["Bitcast"], t_uint, idx_u, idx_const)
        ptr = tid()
        s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr, v_cmds, ci0, idx_u)
        u = tid()
        s.emit(OP["Load"], t_uint, u, ptr)
        f = tid()
        s.emit(OP["Bitcast"], t_float, f, u)
        return f

    def load_tex_int(idx_const: int) -> int:
        idx_u = tid()
        s.emit(OP["Bitcast"], t_uint, idx_u, idx_const)
        ptr = tid()
        s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr, v_tex, ci0, idx_u)
        u = tid()
        s.emit(OP["Load"], t_uint, u, ptr)
        i = tid()
        s.emit(OP["Bitcast"], t_int, i, u)
        return i

    def load_tex_float(idx_int: int) -> int:
        idx_u = tid()
        s.emit(OP["Bitcast"], t_uint, idx_u, idx_int)
        ptr = tid()
        s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr, v_tex, ci0, idx_u)
        u = tid()
        s.emit(OP["Load"], t_uint, u, ptr)
        f = tid()
        s.emit(OP["Bitcast"], t_float, f, u)
        return f

    pc_width = load_hdr_int(ci0)
    pc_height = load_hdr_int(ci1)
    vw = load_hdr_float(ci2)
    vh = load_hdr_float(ci3)
    cc = load_hdr_int(ci4)
    tex_width = load_tex_int(ci0)
    tex_height = load_tex_int(ci1)

    pc_width_u = tid()
    s.emit(OP["Bitcast"], t_uint, pc_width_u, pc_width)
    pc_height_u = tid()
    s.emit(OP["Bitcast"], t_uint, pc_height_u, pc_height)

    gx_oob = tid()
    s.emit(OP["ULessThan"], t_bool, gx_oob, gx_u, pc_width_u)
    gy_oob = tid()
    s.emit(OP["ULessThan"], t_bool, gy_oob, gy_u, pc_height_u)
    in_bounds = tid()
    s.emit(OP["LogicalAnd"], t_bool, in_bounds, gx_oob, gy_oob)

    l_early_ret = s.alloc_id()
    s.emit(OP["SelectionMerge"], l_bounds_merge, 0)
    s.emit(OP["BranchConditional"], in_bounds, l_bounds_merge, l_early_ret)
    s.emit(OP["Label"], l_early_ret)
    s.emit(OP["Return"])
    s.emit(OP["Label"], l_bounds_merge)

    gx_f = tid()
    s.emit(OP["ConvertUToF"], t_float, gx_f, gx_u)
    gy_f = tid()
    s.emit(OP["ConvertUToF"], t_float, gy_f, gy_u)
    pw_f = tid()
    s.emit(OP["ConvertSToF"], t_float, pw_f, pc_width)
    ph_f = tid()
    s.emit(OP["ConvertSToF"], t_float, ph_f, pc_height)
    tex_w_f = tid()
    s.emit(OP["ConvertSToF"], t_float, tex_w_f, tex_width)
    tex_h_f = tid()
    s.emit(OP["ConvertSToF"], t_float, tex_h_f, tex_height)

    vx = tid()
    t1 = tid()
    s.emit(OP["FDiv"], t_float, t1, gx_f, pw_f)
    s.emit(OP["FMul"], t_float, vx, t1, vw)
    vy = tid()
    t2 = tid()
    s.emit(OP["FDiv"], t_float, t2, gy_f, ph_f)
    s.emit(OP["FMul"], t_float, vy, t2, vh)

    lw_ratio = tid()
    s.emit(OP["FDiv"], t_float, lw_ratio, vw, pw_f)
    line_width = tid()
    s.emit(OP["ExtInst"], t_float, line_width, glsl_ext, GLSL["FMax"], lw_ratio, cf15)

    s.emit(OP["Branch"], l_loop_hdr)
    s.emit(OP["Label"], l_loop_hdr)

    phi_i = tid()
    phi_i_pos = len(s.words)
    s.emit(OP["Phi"], t_int, phi_i, ci0, l_bounds_merge, 0, l_loop_cont)
    phi_cr = tid()
    phi_cr_pos = len(s.words)
    s.emit(OP["Phi"], t_float, phi_cr, cf010, l_bounds_merge, 0, l_loop_cont)
    phi_cg = tid()
    phi_cg_pos = len(s.words)
    s.emit(OP["Phi"], t_float, phi_cg, cf011, l_bounds_merge, 0, l_loop_cont)
    phi_cb = tid()
    phi_cb_pos = len(s.words)
    s.emit(OP["Phi"], t_float, phi_cb, cf014, l_bounds_merge, 0, l_loop_cont)
    phi_depth = tid()
    phi_depth_pos = len(s.words)
    s.emit(OP["Phi"], t_float, phi_depth, cf0, l_bounds_merge, 0, l_loop_cont)

    loop_cond = tid()
    s.emit(OP["SLessThan"], t_bool, loop_cond, phi_i, cc)
    s.emit(OP["LoopMerge"], l_loop_merge, l_loop_cont, 0)
    s.emit(OP["BranchConditional"], loop_cond, l_loop_body, l_loop_merge)

    s.emit(OP["Label"], l_loop_body)
    base_mul = tid()
    s.emit(OP["IMul"], t_int, base_mul, phi_i, ci12)
    base = tid()
    s.emit(OP["IAdd"], t_int, base, base_mul, ci5)

    idx_op = tid()
    s.emit(OP["Bitcast"], t_uint, idx_op, base)
    ptr_op = tid()
    s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr_op, v_cmds, ci0, idx_op)
    op_u = tid()
    s.emit(OP["Load"], t_uint, op_u, ptr_op)
    op_i = tid()
    s.emit(OP["Bitcast"], t_int, op_i, op_u)

    def load_float_at(offset_ci: int) -> int:
        idx = tid()
        off = tid()
        s.emit(OP["IAdd"], t_int, off, base, offset_ci)
        s.emit(OP["Bitcast"], t_uint, idx, off)
        p = tid()
        s.emit(OP["AccessChain"], t_ptr_sb_uint, p, v_cmds, ci0, idx)
        u = tid()
        s.emit(OP["Load"], t_uint, u, p)
        f = tid()
        s.emit(OP["Bitcast"], t_float, f, u)
        return f

    x0 = load_float_at(ci1)
    y0 = load_float_at(ci2)
    x1 = load_float_at(ci3)
    y1 = load_float_at(ci4)
    x2 = load_float_at(ci5)
    y2 = load_float_at(ci6)
    cmd_depth = load_float_at(ci7)
    cmd_r = load_float_at(ci9)
    cmd_g = load_float_at(ci10)
    cmd_b = load_float_at(ci11)

    cond_op0 = tid()
    s.emit(OP["IEqual"], t_bool, cond_op0, op_i, ci0)
    s.emit(OP["SelectionMerge"], l_op0_merge, 0)
    s.emit(OP["BranchConditional"], cond_op0, l_op0_true, l_op0_merge)
    s.emit(OP["Label"], l_op0_true)
    s.emit(OP["Branch"], l_op0_merge)
    s.emit(OP["Label"], l_op0_merge)
    cr_after0 = tid()
    cg_after0 = tid()
    cb_after0 = tid()
    depth_after0 = tid()
    s.emit(OP["Phi"], t_float, cr_after0, cmd_r, l_op0_true, phi_cr, l_loop_body)
    s.emit(OP["Phi"], t_float, cg_after0, cmd_g, l_op0_true, phi_cg, l_loop_body)
    s.emit(OP["Phi"], t_float, cb_after0, cmd_b, l_op0_true, phi_cb, l_loop_body)
    s.emit(OP["Phi"], t_float, depth_after0, cf0, l_op0_true, phi_depth, l_loop_body)

    cond_op1 = tid()
    s.emit(OP["IEqual"], t_bool, cond_op1, op_i, ci1)
    s.emit(OP["SelectionMerge"], l_op1_merge, 0)
    s.emit(OP["BranchConditional"], cond_op1, l_op1_true, l_op1_merge)

    s.emit(OP["Label"], l_op1_true)
    x0px1 = tid()
    y0py1 = tid()
    s.emit(OP["FAdd"], t_float, x0px1, x0, x1)
    s.emit(OP["FAdd"], t_float, y0py1, y0, y1)
    c1 = tid()
    c2 = tid()
    c3 = tid()
    c4 = tid()
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, c1, vx, x0)
    s.emit(OP["FOrdLessThan"], t_bool, c2, vx, x0px1)
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, c3, vy, y0)
    s.emit(OP["FOrdLessThan"], t_bool, c4, vy, y0py1)
    ca = tid()
    cb_cond = tid()
    s.emit(OP["LogicalAnd"], t_bool, ca, c1, c2)
    s.emit(OP["LogicalAnd"], t_bool, cb_cond, c3, c4)
    rect_hit = tid()
    s.emit(OP["LogicalAnd"], t_bool, rect_hit, ca, cb_cond)
    s.emit(OP["SelectionMerge"], l_rect_merge, 0)
    s.emit(OP["BranchConditional"], rect_hit, l_rect_inner, l_rect_merge)
    s.emit(OP["Label"], l_rect_inner)
    s.emit(OP["Branch"], l_rect_merge)
    s.emit(OP["Label"], l_rect_merge)
    cr_rect = tid()
    cg_rect = tid()
    cb_rect = tid()
    depth_rect = tid()
    s.emit(OP["Phi"], t_float, cr_rect, cmd_r, l_rect_inner, cr_after0, l_op1_true)
    s.emit(OP["Phi"], t_float, cg_rect, cmd_g, l_rect_inner, cg_after0, l_op1_true)
    s.emit(OP["Phi"], t_float, cb_rect, cmd_b, l_rect_inner, cb_after0, l_op1_true)
    s.emit(OP["Phi"], t_float, depth_rect, depth_after0, l_rect_inner, depth_after0, l_op1_true)
    s.emit(OP["Branch"], l_op1_merge)

    s.emit(OP["Label"], l_op1_merge)
    cr_after1 = tid()
    cg_after1 = tid()
    cb_after1 = tid()
    depth_after1 = tid()
    s.emit(OP["Phi"], t_float, cr_after1, cr_rect, l_rect_merge, cr_after0, l_op0_merge)
    s.emit(OP["Phi"], t_float, cg_after1, cg_rect, l_rect_merge, cg_after0, l_op0_merge)
    s.emit(OP["Phi"], t_float, cb_after1, cb_rect, l_rect_merge, cb_after0, l_op0_merge)
    s.emit(OP["Phi"], t_float, depth_after1, depth_rect, l_rect_merge, depth_after0, l_op0_merge)

    cond_op2 = tid()
    s.emit(OP["IEqual"], t_bool, cond_op2, op_i, ci2)
    s.emit(OP["SelectionMerge"], l_op2_merge, 0)
    s.emit(OP["BranchConditional"], cond_op2, l_op2_true, l_op2_merge)

    s.emit(OP["Label"], l_op2_true)
    abx = tid()
    aby = tid()
    s.emit(OP["FSub"], t_float, abx, x1, x0)
    s.emit(OP["FSub"], t_float, aby, y1, y0)
    len2_a = tid()
    len2_b = tid()
    len2 = tid()
    s.emit(OP["FMul"], t_float, len2_a, abx, abx)
    s.emit(OP["FMul"], t_float, len2_b, aby, aby)
    s.emit(OP["FAdd"], t_float, len2, len2_a, len2_b)
    pax = tid()
    pay = tid()
    s.emit(OP["FSub"], t_float, pax, vx, x0)
    s.emit(OP["FSub"], t_float, pay, vy, y0)
    dot_pa_ab_x = tid()
    dot_pa_ab_y = tid()
    dot_pa_ab = tid()
    s.emit(OP["FMul"], t_float, dot_pa_ab_x, pax, abx)
    s.emit(OP["FMul"], t_float, dot_pa_ab_y, pay, aby)
    s.emit(OP["FAdd"], t_float, dot_pa_ab, dot_pa_ab_x, dot_pa_ab_y)
    t_raw = tid()
    s.emit(OP["FDiv"], t_float, t_raw, dot_pa_ab, len2)
    t_clamp = tid()
    s.emit(OP["ExtInst"], t_float, t_clamp, glsl_ext, GLSL["FClamp"], t_raw, cf0, cf1)
    proj_x_off = tid()
    proj_y_off = tid()
    proj_x = tid()
    proj_y = tid()
    s.emit(OP["FMul"], t_float, proj_x_off, abx, t_clamp)
    s.emit(OP["FMul"], t_float, proj_y_off, aby, t_clamp)
    s.emit(OP["FAdd"], t_float, proj_x, x0, proj_x_off)
    s.emit(OP["FAdd"], t_float, proj_y, y0, proj_y_off)
    dx = tid()
    dy = tid()
    dx2 = tid()
    dy2 = tid()
    dist2 = tid()
    dist = tid()
    s.emit(OP["FSub"], t_float, dx, vx, proj_x)
    s.emit(OP["FSub"], t_float, dy, vy, proj_y)
    s.emit(OP["FMul"], t_float, dx2, dx, dx)
    s.emit(OP["FMul"], t_float, dy2, dy, dy)
    s.emit(OP["FAdd"], t_float, dist2, dx2, dy2)
    s.emit(OP["ExtInst"], t_float, dist, glsl_ext, GLSL["Sqrt"], dist2)
    line_hit = tid()
    s.emit(OP["FOrdLessThanEqual"], t_bool, line_hit, dist, line_width)
    s.emit(OP["SelectionMerge"], l_line_merge, 0)
    s.emit(OP["BranchConditional"], line_hit, l_line_inner, l_line_merge)
    s.emit(OP["Label"], l_line_inner)
    s.emit(OP["Branch"], l_line_merge)
    s.emit(OP["Label"], l_line_merge)
    cr_line = tid()
    cg_line = tid()
    cb_line = tid()
    depth_line = tid()
    s.emit(OP["Phi"], t_float, cr_line, cr_after1, l_line_inner, cmd_r, l_op2_true)
    s.emit(OP["Phi"], t_float, cg_line, cg_after1, l_line_inner, cmd_g, l_op2_true)
    s.emit(OP["Phi"], t_float, cb_line, cb_after1, l_line_inner, cmd_b, l_op2_true)
    s.emit(OP["Phi"], t_float, depth_line, depth_after1, l_line_inner, depth_after1, l_op2_true)
    s.emit(OP["Branch"], l_op2_merge)

    s.emit(OP["Label"], l_op2_merge)
    cr_after2 = tid()
    cg_after2 = tid()
    cb_after2 = tid()
    depth_after2 = tid()
    s.emit(OP["Phi"], t_float, cr_after2, cr_line, l_line_merge, cr_after1, l_op1_merge)
    s.emit(OP["Phi"], t_float, cg_after2, cg_line, l_line_merge, cg_after1, l_op1_merge)
    s.emit(OP["Phi"], t_float, cb_after2, cb_line, l_line_merge, cb_after1, l_op1_merge)
    s.emit(OP["Phi"], t_float, depth_after2, depth_line, l_line_merge, depth_after1, l_op1_merge)

    cond_op3 = tid()
    s.emit(OP["IEqual"], t_bool, cond_op3, op_i, ci3)
    s.emit(OP["SelectionMerge"], l_op3_merge, 0)
    s.emit(OP["BranchConditional"], cond_op3, l_op3_true, l_op3_merge)

    s.emit(OP["Label"], l_op3_true)
    e0a = tid()
    e0b = tid()
    e0 = tid()
    s.emit(OP["FSub"], t_float, e0a, vx, x1)
    s.emit(OP["FSub"], t_float, e0b, y2, y1)
    e0c = tid()
    e0d = tid()
    e0e = tid()
    s.emit(OP["FMul"], t_float, e0c, e0a, e0b)
    s.emit(OP["FSub"], t_float, e0d, vy, y1)
    s.emit(OP["FSub"], t_float, e0e, x2, x1)
    e0f = tid()
    s.emit(OP["FMul"], t_float, e0f, e0d, e0e)
    s.emit(OP["FSub"], t_float, e0, e0c, e0f)

    e1a = tid()
    e1b = tid()
    e1 = tid()
    s.emit(OP["FSub"], t_float, e1a, vx, x2)
    s.emit(OP["FSub"], t_float, e1b, y0, y2)
    e1c = tid()
    e1d = tid()
    e1e = tid()
    s.emit(OP["FMul"], t_float, e1c, e1a, e1b)
    s.emit(OP["FSub"], t_float, e1d, vy, y2)
    s.emit(OP["FSub"], t_float, e1e, x0, x2)
    e1f = tid()
    s.emit(OP["FMul"], t_float, e1f, e1d, e1e)
    s.emit(OP["FSub"], t_float, e1, e1c, e1f)

    e2a = tid()
    e2b = tid()
    e2 = tid()
    s.emit(OP["FSub"], t_float, e2a, vx, x0)
    s.emit(OP["FSub"], t_float, e2b, y1, y0)
    e2c = tid()
    e2d = tid()
    e2e = tid()
    s.emit(OP["FMul"], t_float, e2c, e2a, e2b)
    s.emit(OP["FSub"], t_float, e2d, vy, y0)
    s.emit(OP["FSub"], t_float, e2e, x1, x0)
    e2f = tid()
    s.emit(OP["FMul"], t_float, e2f, e2d, e2e)
    s.emit(OP["FSub"], t_float, e2, e2c, e2f)

    e0_pos = tid()
    e1_pos = tid()
    e2_pos = tid()
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, e0_pos, e0, cf0)
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, e1_pos, e1, cf0)
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, e2_pos, e2, cf0)
    tri_all_pos_a = tid()
    tri_all_pos = tid()
    s.emit(OP["LogicalAnd"], t_bool, tri_all_pos_a, e0_pos, e1_pos)
    s.emit(OP["LogicalAnd"], t_bool, tri_all_pos, tri_all_pos_a, e2_pos)

    e0_neg = tid()
    e1_neg = tid()
    e2_neg = tid()
    s.emit(OP["FOrdLessThanEqual"], t_bool, e0_neg, e0, cf0)
    s.emit(OP["FOrdLessThanEqual"], t_bool, e1_neg, e1, cf0)
    s.emit(OP["FOrdLessThanEqual"], t_bool, e2_neg, e2, cf0)
    tri_all_neg_a = tid()
    tri_all_neg = tid()
    s.emit(OP["LogicalAnd"], t_bool, tri_all_neg_a, e0_neg, e1_neg)
    s.emit(OP["LogicalAnd"], t_bool, tri_all_neg, tri_all_neg_a, e2_neg)

    tri_hit = tid()
    s.emit(OP["LogicalOr"], t_bool, tri_hit, tri_all_pos, tri_all_neg)
    depth_pass = tid()
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, depth_pass, cmd_depth, depth_after2)
    tri_write = tid()
    s.emit(OP["LogicalAnd"], t_bool, tri_write, tri_hit, depth_pass)
    s.emit(OP["SelectionMerge"], l_tri_merge, 0)
    s.emit(OP["BranchConditional"], tri_write, l_tri_inner, l_tri_merge)
    s.emit(OP["Label"], l_tri_inner)
    s.emit(OP["Branch"], l_tri_merge)
    s.emit(OP["Label"], l_tri_merge)
    cr_tri = tid()
    cg_tri = tid()
    cb_tri = tid()
    depth_tri = tid()
    s.emit(OP["Phi"], t_float, cr_tri, cmd_r, l_tri_inner, cr_after2, l_op3_true)
    s.emit(OP["Phi"], t_float, cg_tri, cmd_g, l_tri_inner, cg_after2, l_op3_true)
    s.emit(OP["Phi"], t_float, cb_tri, cmd_b, l_tri_inner, cb_after2, l_op3_true)
    s.emit(OP["Phi"], t_float, depth_tri, cmd_depth, l_tri_inner, depth_after2, l_op3_true)
    s.emit(OP["Branch"], l_op3_merge)

    s.emit(OP["Label"], l_op3_merge)
    cr_after3 = tid()
    cg_after3 = tid()
    cb_after3 = tid()
    depth_after3 = tid()
    s.emit(OP["Phi"], t_float, cr_after3, cr_tri, l_tri_merge, cr_after2, l_op2_merge)
    s.emit(OP["Phi"], t_float, cg_after3, cg_tri, l_tri_merge, cg_after2, l_op2_merge)
    s.emit(OP["Phi"], t_float, cb_after3, cb_tri, l_tri_merge, cb_after2, l_op2_merge)
    s.emit(OP["Phi"], t_float, depth_after3, depth_tri, l_tri_merge, depth_after2, l_op2_merge)

    cond_op4 = tid()
    s.emit(OP["IEqual"], t_bool, cond_op4, op_i, ci4)
    s.emit(OP["SelectionMerge"], l_op4_merge, 0)
    s.emit(OP["BranchConditional"], cond_op4, l_op4_true, l_op4_merge)

    s.emit(OP["Label"], l_op4_true)
    x0px1_t = tid()
    y0py1_t = tid()
    s.emit(OP["FAdd"], t_float, x0px1_t, x0, x1)
    s.emit(OP["FAdd"], t_float, y0py1_t, y0, y1)
    txc1 = tid()
    txc2 = tid()
    txc3 = tid()
    txc4 = tid()
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, txc1, vx, x0)
    s.emit(OP["FOrdLessThan"], t_bool, txc2, vx, x0px1_t)
    s.emit(OP["FOrdGreaterThanEqual"], t_bool, txc3, vy, y0)
    s.emit(OP["FOrdLessThan"], t_bool, txc4, vy, y0py1_t)
    txca = tid()
    txcb = tid()
    s.emit(OP["LogicalAnd"], t_bool, txca, txc1, txc2)
    s.emit(OP["LogicalAnd"], t_bool, txcb, txc3, txc4)
    tex_hit = tid()
    s.emit(OP["LogicalAnd"], t_bool, tex_hit, txca, txcb)
    s.emit(OP["SelectionMerge"], l_tex_merge, 0)
    s.emit(OP["BranchConditional"], tex_hit, l_tex_inner, l_tex_merge)

    s.emit(OP["Label"], l_tex_inner)
    uvx_num = tid()
    uvy_num = tid()
    uvx_raw = tid()
    uvy_raw = tid()
    uvx = tid()
    uvy = tid()
    s.emit(OP["FSub"], t_float, uvx_num, vx, x0)
    s.emit(OP["FSub"], t_float, uvy_num, vy, y0)
    s.emit(OP["FDiv"], t_float, uvx_raw, uvx_num, x1)
    s.emit(OP["FDiv"], t_float, uvy_raw, uvy_num, y1)
    s.emit(OP["ExtInst"], t_float, uvx, glsl_ext, GLSL["FClamp"], uvx_raw, cf0, cf0999)
    s.emit(OP["ExtInst"], t_float, uvy, glsl_ext, GLSL["FClamp"], uvy_raw, cf0, cf0999)
    tex_fx = tid()
    tex_fy = tid()
    tex_ix_u = tid()
    tex_iy_u = tid()
    tex_ix = tid()
    tex_iy = tid()
    s.emit(OP["FMul"], t_float, tex_fx, uvx, tex_w_f)
    s.emit(OP["FMul"], t_float, tex_fy, uvy, tex_h_f)
    s.emit(OP["ConvertFToU"], t_uint, tex_ix_u, tex_fx)
    s.emit(OP["ConvertFToU"], t_uint, tex_iy_u, tex_fy)
    s.emit(OP["Bitcast"], t_int, tex_ix, tex_ix_u)
    s.emit(OP["Bitcast"], t_int, tex_iy, tex_iy_u)
    tex_row = tid()
    tex_flat = tid()
    tex_off3a = tid()
    tex_off3 = tid()
    tex_r_idx = tid()
    tex_g_idx = tid()
    tex_b_idx = tid()
    s.emit(OP["IMul"], t_int, tex_row, tex_iy, tex_width)
    s.emit(OP["IAdd"], t_int, tex_flat, tex_row, tex_ix)
    s.emit(OP["IMul"], t_int, tex_off3a, tex_flat, ci3)
    s.emit(OP["IAdd"], t_int, tex_off3, tex_off3a, ci2)
    s.emit(OP["IAdd"], t_int, tex_r_idx, tex_off3, ci0)
    s.emit(OP["IAdd"], t_int, tex_g_idx, tex_off3, ci1)
    s.emit(OP["IAdd"], t_int, tex_b_idx, tex_off3, ci2)
    tex_r = load_tex_float(tex_r_idx)
    tex_g = load_tex_float(tex_g_idx)
    tex_b = load_tex_float(tex_b_idx)
    s.emit(OP["Branch"], l_tex_merge)

    s.emit(OP["Label"], l_tex_merge)
    cr_tex = tid()
    cg_tex = tid()
    cb_tex = tid()
    depth_tex = tid()
    s.emit(OP["Phi"], t_float, cr_tex, tex_r, l_tex_inner, cr_after3, l_op4_true)
    s.emit(OP["Phi"], t_float, cg_tex, tex_g, l_tex_inner, cg_after3, l_op4_true)
    s.emit(OP["Phi"], t_float, cb_tex, tex_b, l_tex_inner, cb_after3, l_op4_true)
    s.emit(OP["Phi"], t_float, depth_tex, depth_after3, l_tex_inner, depth_after3, l_op4_true)
    s.emit(OP["Branch"], l_op4_merge)

    s.emit(OP["Label"], l_op4_merge)
    cr_new = tid()
    cg_new = tid()
    cb_new = tid()
    depth_new = tid()
    s.emit(OP["Phi"], t_float, cr_new, cr_tex, l_tex_merge, cr_after3, l_op3_merge)
    s.emit(OP["Phi"], t_float, cg_new, cg_tex, l_tex_merge, cg_after3, l_op3_merge)
    s.emit(OP["Phi"], t_float, cb_new, cb_tex, l_tex_merge, cb_after3, l_op3_merge)
    s.emit(OP["Phi"], t_float, depth_new, depth_tex, l_tex_merge, depth_after3, l_op3_merge)
    s.emit(OP["Branch"], l_loop_cont)

    s.emit(OP["Label"], l_loop_cont)
    i_next = tid()
    s.emit(OP["IAdd"], t_int, i_next, phi_i, ci1)
    s.words[phi_i_pos + 5] = i_next
    s.words[phi_i_pos + 6] = l_loop_cont
    s.words[phi_cr_pos + 5] = cr_new
    s.words[phi_cr_pos + 6] = l_loop_cont
    s.words[phi_cg_pos + 5] = cg_new
    s.words[phi_cg_pos + 6] = l_loop_cont
    s.words[phi_cb_pos + 5] = cb_new
    s.words[phi_cb_pos + 6] = l_loop_cont
    s.words[phi_depth_pos + 5] = depth_new
    s.words[phi_depth_pos + 6] = l_loop_cont
    s.emit(OP["Branch"], l_loop_hdr)

    s.emit(OP["Label"], l_loop_merge)
    cr_c = tid()
    cg_c = tid()
    cb_c = tid()
    s.emit(OP["ExtInst"], t_float, cr_c, glsl_ext, GLSL["FClamp"], phi_cr, cf0, cf1)
    s.emit(OP["ExtInst"], t_float, cg_c, glsl_ext, GLSL["FClamp"], phi_cg, cf0, cf1)
    s.emit(OP["ExtInst"], t_float, cb_c, glsl_ext, GLSL["FClamp"], phi_cb, cf0, cf1)
    cr_255 = tid()
    cg_255 = tid()
    cb_255 = tid()
    s.emit(OP["FMul"], t_float, cr_255, cr_c, cf255)
    s.emit(OP["FMul"], t_float, cg_255, cg_c, cf255)
    s.emit(OP["FMul"], t_float, cb_255, cb_c, cf255)
    ru = tid()
    gu = tid()
    bu = tid()
    s.emit(OP["ConvertFToU"], t_uint, ru, cr_255)
    s.emit(OP["ConvertFToU"], t_uint, gu, cg_255)
    s.emit(OP["ConvertFToU"], t_uint, bu, cb_255)
    g8 = tid()
    b16 = tid()
    s.emit(OP["ShiftLeftLogical"], t_uint, g8, gu, cu8)
    s.emit(OP["ShiftLeftLogical"], t_uint, b16, bu, cu16)
    pix0 = tid()
    pix1 = tid()
    pixel = tid()
    s.emit(OP["BitwiseOr"], t_uint, pix0, ru, g8)
    s.emit(OP["BitwiseOr"], t_uint, pix1, pix0, b16)
    s.emit(OP["BitwiseOr"], t_uint, pixel, pix1, cu_alpha)

    gy_i = tid()
    s.emit(OP["Bitcast"], t_int, gy_i, gy_u)
    idx_row = tid()
    s.emit(OP["IMul"], t_int, idx_row, gy_i, pc_width)
    gx_i = tid()
    s.emit(OP["Bitcast"], t_int, gx_i, gx_u)
    idx_flat = tid()
    s.emit(OP["IAdd"], t_int, idx_flat, idx_row, gx_i)
    idx_u = tid()
    s.emit(OP["Bitcast"], t_uint, idx_u, idx_flat)
    ptr_pix = tid()
    s.emit(OP["AccessChain"], t_ptr_sb_uint, ptr_pix, v_pixels, ci0, idx_u)
    s.emit(OP["Store"], ptr_pix, pixel)

    s.emit(OP["Return"])
    s.emit(OP["FunctionEnd"])
    return s.done()


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: build_spirv_dat.py <out-path>", file=sys.stderr)
        return 2

    out_path = pathlib.Path(argv[1]).expanduser()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(build())
    print(str(out_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

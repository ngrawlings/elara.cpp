// 32-bit RGBA bitmap helpers for E.
//
// The OS-facing default surface format is one u32 per pixel:
//   bits  0..7   R
//   bits  8..15  G
//   bits 16..23  B
//   bits 24..31  A
//
// Bitmap storage is a local byte span. Pixel byte offset is:
//   bitmap_off + ((y * width + x) * 4)

#include "common/bytes.em"

function int rgba(int r, int g, int b, int a) {
  int color = 0;

  EPA {
    LOAD_LW 0
    PUSH 255
    AND_I32
    LOAD_LW 1
    PUSH 255
    AND_I32
    PUSH 256
    MUL_I32
    OR_I32
    LOAD_LW 2
    PUSH 255
    AND_I32
    PUSH 65536
    MUL_I32
    OR_I32
    LOAD_LW 3
    PUSH 255
    AND_I32
    PUSH 16777216
    MUL_I32
    OR_I32
    STORE_LW 4
  }

  color = color;
  return color;
}

function int rgba_r(int color) {
  return byte_and(color, 255);
}

function int rgba_g(int color) {
  int value = 0;

  EPA {
    LOAD_LW 0
    PUSH 256
    DIV_I32
    PUSH 255
    AND_I32
    STORE_LW 1
  }

  value = value;
  return value;
}

function int rgba_b(int color) {
  int value = 0;

  EPA {
    LOAD_LW 0
    PUSH 65536
    DIV_I32
    PUSH 255
    AND_I32
    STORE_LW 1
  }

  value = value;
  return value;
}

function int rgba_a(int color) {
  int value = 0;

  EPA {
    LOAD_LW 0
    PUSH 16777216
    DIV_I32
    PUSH 255
    AND_I32
    STORE_LW 1
  }

  value = value;
  return value;
}

function int bitmap_required_bytes(int width, int height) {
  return width * height * 4;
}

function int bitmap_pixel_offset(int bitmap_off, int width, int x, int y) {
  return bitmap_off + (((y * width) + x) * 4);
}

function int bitmap_get_pixel(int bitmap_off, int width, int x, int y) {
  int off = bitmap_pixel_offset(bitmap_off, width, x, y);
  return u32_load_le(off);
}

function int bitmap_set_pixel(int bitmap_off, int width, int x, int y, int color) {
  int off = bitmap_pixel_offset(bitmap_off, width, x, y);
  u32_store_le(off, color);
  return color;
}

function int bitmap_clear(int bitmap_off, int width, int height, int color) {
  int written = 0;
  int total = width * height;

  EPA {
    SET_R 1 0
    E_BITMAP_CLEAR_LOOP:
    PUSH R1
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_BITMAP_CLEAR_DONE
    LOAD_LW 0
    PUSH R1
    PUSH 4
    MUL_I32
    ADD_I32
    POP R2
    LOAD_LW 3
    POP R3
    RLB_MOV4 R3 R2
    INC R1
    JMP E_BITMAP_CLEAR_LOOP
    E_BITMAP_CLEAR_DONE:
    PUSH R1
    PUSH 4
    MUL_I32
    STORE_LW 4
  }

  total = total;
  written = written;
  return written;
}

function int bitmap_fill_rect(
  int bitmap_off,
  int width,
  int height,
  int x,
  int y,
  int rect_w,
  int rect_h,
  int color
) {
  int written = 0;
  int row = 0;
  int col = 0;

  EPA {
    SET_R 1 0
    E_BITMAP_RECT_ROW:
    PUSH R1
    LOAD_LW 6
    LT_I32
    POP R0
    JZ E_BITMAP_RECT_DONE
    LOAD_LW 4
    PUSH R1
    ADD_I32
    LOAD_LW 2
    LT_I32
    POP R0
    JZ E_BITMAP_RECT_NEXT_ROW
    SET_R 2 0
    E_BITMAP_RECT_COL:
    PUSH R2
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_BITMAP_RECT_NEXT_ROW
    LOAD_LW 3
    PUSH R2
    ADD_I32
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BITMAP_RECT_NEXT_COL
    LOAD_LW 4
    PUSH R1
    ADD_I32
    LOAD_LW 1
    MUL_I32
    LOAD_LW 3
    ADD_I32
    PUSH R2
    ADD_I32
    PUSH 4
    MUL_I32
    LOAD_LW 0
    ADD_I32
    POP R3
    LOAD_LW 7
    POP R0
    RLB_MOV4 R0 R3
    LOAD_LW 8
    PUSH 4
    ADD_I32
    STORE_LW 8
    E_BITMAP_RECT_NEXT_COL:
    INC R2
    JMP E_BITMAP_RECT_COL
    E_BITMAP_RECT_NEXT_ROW:
    INC R1
    JMP E_BITMAP_RECT_ROW
    E_BITMAP_RECT_DONE:
  }

  col = col;
  row = row;
  written = written;
  return written;
}

function int bitmap_blit(
  int dst_off,
  int dst_width,
  int dst_height,
  int dst_x,
  int dst_y,
  int src_off,
  int src_width,
  int src_height,
  int src_x,
  int src_y,
  int copy_w,
  int copy_h
) {
  int written = 0;
  int row = 0;
  int col = 0;

  EPA {
    SET_R 1 0
    E_BITMAP_BLIT_ROW:
    PUSH R1
    LOAD_LW 11
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_DONE
    LOAD_LW 4
    PUSH R1
    ADD_I32
    LOAD_LW 2
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_NEXT_ROW
    LOAD_LW 9
    PUSH R1
    ADD_I32
    LOAD_LW 7
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_NEXT_ROW
    SET_R 2 0
    E_BITMAP_BLIT_COL:
    PUSH R2
    LOAD_LW 10
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_NEXT_ROW
    LOAD_LW 3
    PUSH R2
    ADD_I32
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_NEXT_COL
    LOAD_LW 8
    PUSH R2
    ADD_I32
    LOAD_LW 6
    LT_I32
    POP R0
    JZ E_BITMAP_BLIT_NEXT_COL
    LOAD_LW 9
    PUSH R1
    ADD_I32
    LOAD_LW 6
    MUL_I32
    LOAD_LW 8
    ADD_I32
    PUSH R2
    ADD_I32
    PUSH 4
    MUL_I32
    LOAD_LW 5
    ADD_I32
    POP R3
    LBR_MOV4 R0 R3
    LOAD_LW 4
    PUSH R1
    ADD_I32
    LOAD_LW 1
    MUL_I32
    LOAD_LW 3
    ADD_I32
    PUSH R2
    ADD_I32
    PUSH 4
    MUL_I32
    LOAD_LW 0
    ADD_I32
    POP R3
    RLB_MOV4 R0 R3
    LOAD_LW 12
    PUSH 4
    ADD_I32
    STORE_LW 12
    E_BITMAP_BLIT_NEXT_COL:
    INC R2
    JMP E_BITMAP_BLIT_COL
    E_BITMAP_BLIT_NEXT_ROW:
    INC R1
    JMP E_BITMAP_BLIT_ROW
    E_BITMAP_BLIT_DONE:
  }

  col = col;
  row = row;
  written = written;
  return written;
}

function int bitmap_blend_over_pixel(int dst_color, int src_color) {
  int out = 0;
  int sr = rgba_r(src_color);
  int sg = rgba_g(src_color);
  int sb = rgba_b(src_color);
  int sa = rgba_a(src_color);
  int dr = rgba_r(dst_color);
  int dg = rgba_g(dst_color);
  int db = rgba_b(dst_color);
  int da = rgba_a(dst_color);
  int inv_a = 255 - sa;
  int r = ((sr * sa) + (dr * inv_a)) / 255;
  int g = ((sg * sa) + (dg * inv_a)) / 255;
  int b = ((sb * sa) + (db * inv_a)) / 255;
  int a = sa + ((da * inv_a) / 255);

  out = rgba(r, g, b, a);
  return out;
}

function int bitmap_blend_pixel(int bitmap_off, int width, int x, int y, int src_color) {
  int dst = bitmap_get_pixel(bitmap_off, width, x, y);
  int out = bitmap_blend_over_pixel(dst, src_color);
  bitmap_set_pixel(bitmap_off, width, x, y, out);
  return out;
}

#include "common/bitmap.em"

type BitmapPrimitiveIngress(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.bitmap.primitives");
  start_worker(main);
}

worker main(BitmapPrimitiveIngress msg) {
  int red = rgba(255, 0, 0, 255);
  int blue = rgba(0, 0, 255, 255);
  int transparent_green = rgba(0, 255, 0, 128);
  int bytes = bitmap_required_bytes(8, 8);
  int off = bitmap_pixel_offset(0, 8, 2, 3);
  int cleared = bitmap_clear(0, 8, 8, blue);
  int set = bitmap_set_pixel(0, 8, 2, 3, red);
  int got = bitmap_get_pixel(0, 8, 2, 3);
  int filled = bitmap_fill_rect(0, 8, 8, 1, 1, 4, 3, red);
  int blended = bitmap_blend_over_pixel(blue, transparent_green);
  int blend_write = bitmap_blend_pixel(0, 8, 4, 4, transparent_green);
  int copied = bitmap_blit(128, 8, 8, 0, 0, 0, 8, 8, 0, 0, 8, 8);
}

#include "epa_ring_buffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int epa_ring_init(IdRing *r, uint32_t cap) {
  if (!r || cap == 0) return 0;
  memset(r, 0, sizeof(*r));
  r->buf = (uint32_t*)calloc(cap, sizeof(uint32_t));
  if (!r->buf) return 0;
  r->cap = cap;
  r->head = r->tail = r->count = 0;
  return 1;
}

void epa_ring_free(IdRing *r) {
  if (!r) return;
  free(r->buf);
  r->buf = NULL;
  r->cap = r->head = r->tail = r->count = 0;
}

void epa_ring_clear(IdRing *r) {
  if (!r) return;
  r->head = r->tail = r->count = 0;
}

static int ring_push(IdRing *r, uint32_t id, int soft, char err[256]) {
  // soft=1 => overwrite/drop oldest on full; soft=0 => error on full
  if (r->count == r->cap) {
    if (!soft) {
      if (err) snprintf(err, 256, "ring full");
      return 0;
    }
    // drop oldest (advance head)
    r->head = (r->head + 1u) % r->cap;
    r->count--;
  }

  r->buf[r->tail] = id;
  r->tail = (r->tail + 1u) % r->cap;
  r->count++;
  return 1;
}

static int ring_pop(IdRing *r, uint32_t *out_id) {
  if (r->count == 0) return 0;
  if (out_id) *out_id = r->buf[r->head];
  r->head = (r->head + 1u) % r->cap;
  r->count--;
  return 1;
}

int epa_ring_push(IdRing *r, uint32_t id, int soft, char err[256]) {
  if (!r || !r->buf || r->cap == 0) {
    if (err) snprintf(err, 256, "ring not initialized");
    return 0;
  }
  return ring_push(r, id, soft, err);
}

int epa_ring_pop(IdRing *r, uint32_t *out_id) {
  if (!r || !r->buf || r->cap == 0) return 0;
  return ring_pop(r, out_id);
}

uint32_t epa_ring_count(const IdRing *r) {
  if (!r) return 0;
  return r->count;
}

uint32_t epa_ring_space(const IdRing *r) {
  if (!r) return 0;
  if (r->cap < r->count) return 0; // safety
  return r->cap - r->count;
}


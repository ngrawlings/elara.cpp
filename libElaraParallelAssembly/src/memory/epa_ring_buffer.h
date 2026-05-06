#pragma once
#include <stdint.h>

typedef struct {
  uint32_t *buf;
  uint32_t cap;      // fixed
  uint32_t head;     // pop index
  uint32_t tail;     // push index
  uint32_t count;    // size
} IdRing;

int  epa_ring_init(IdRing *r, uint32_t cap);
void epa_ring_free(IdRing *r);
void epa_ring_clear(IdRing *r);
int  epa_ring_push(IdRing *r, uint32_t id, int soft, char err[256]);
int  epa_ring_pop(IdRing *r, uint32_t *out_id);
uint32_t epa_ring_count(const IdRing *r);
uint32_t epa_ring_space(const IdRing *r);


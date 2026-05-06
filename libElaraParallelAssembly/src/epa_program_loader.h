#pragma once
#include <stdint.h>
#include <stddef.h>
#include "epa_program_desc.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

// Load a compiled EPA blob into a program descriptor (fills entries + funcs)
int epa_program_load_from_blob(
    EpaProgramDesc *out_prog,
    const uint8_t *blob, size_t blob_len,
    char err[EPA_MAX_ERR]
);

// Free any heap owned by the program descriptor (func table etc.)
void epa_program_free(EpaProgramDesc *prog);

int epa_program_parse(EpaProgramDesc *out, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);

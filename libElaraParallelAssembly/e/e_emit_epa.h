#pragma once

#include "e_ast.h"
#include "e_semantic.h"
#include "e_preprocess.h"

#include <stdio.h>

/*
 * line_map and main_file are optional (may be NULL).
 * When provided, stmt->line values (which are flat preprocessed-source lines)
 * are translated to original-file line numbers before being written to map_out.
 * Lines from included .em files emit 0 in the map.
 */
int e_emit_epa_asm(FILE *out, FILE *map_out,
                   const EProgram *prog, const ESemanticModel *model,
                   const ELineMap *line_map, const char *main_file,
                   char err[256]);

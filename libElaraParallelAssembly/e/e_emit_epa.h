#pragma once

#include "e_ast.h"
#include "e_semantic.h"

#include <stdio.h>

int e_emit_epa_asm(FILE *out, FILE *map_out, const EProgram *prog, const ESemanticModel *model, char err[256]);

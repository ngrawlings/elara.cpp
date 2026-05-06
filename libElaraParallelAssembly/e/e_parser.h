#pragma once

#include "e_lexer.h"
#include "e_ast.h"

int e_parse_program(const ETokenVec *tokens, EProgram *out_program, char err[256]);

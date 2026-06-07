// Unicode-native string helpers for E.
//
// Strings are UTF-8 byte spans: (offset, length). Functions do not return
// strings; callers ask for a byte length, allocate local storage, then pass an
// output span. Formatting v1 treats "{}" as an i32 placeholder and copies all
// other UTF-8 bytes unchanged.

function int unicode_copy_len(int source_off, int source_len) {
  return source_len;
}

function int unicode_copy_into(int out_off, int out_cap, int source_off, int source_len) {
  int written = 0;
  int limit = 0;

  EPA {
    LOAD_LW 3
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_COPY_USE_CAP
    LOAD_LW 3
    STORE_LW 5
    JMP E_STR_COPY_READY
    E_STR_COPY_USE_CAP:
    LOAD_LW 1
    STORE_LW 5
    E_STR_COPY_READY:
    SET_R 1 0
    E_STR_COPY_LOOP:
    PUSH R1
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_STR_COPY_DONE
    LOAD_LW 2
    PUSH R1
    ADD_I32
    POP R2
    LBR_MOV1 R3 R2
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R2
    RLB_MOV1 R3 R2
    INC R1
    JMP E_STR_COPY_LOOP
    E_STR_COPY_DONE:
    PUSH R1
    STORE_LW 4
  }

  limit = limit;
  written = written;
  return written;
}

function int unicode_concat_len(int left_off, int left_len, int right_off, int right_len) {
  return left_len + right_len;
}

function int unicode_concat_into(
  int out_off,
  int out_cap,
  int left_off,
  int left_len,
  int right_off,
  int right_len
) {
  int written = 0;

  EPA {
    SET_R 1 0
    SET_R 2 0
    E_STR_CONCAT_LEFT:
    PUSH R2
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_STR_CONCAT_RIGHT_PREP
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_CONCAT_DONE
    LOAD_LW 2
    PUSH R2
    ADD_I32
    POP R3
    LBR_MOV1 R0 R3
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    RLB_MOV1 R0 R3
    INC R1
    INC R2
    JMP E_STR_CONCAT_LEFT
    E_STR_CONCAT_RIGHT_PREP:
    SET_R 2 0
    E_STR_CONCAT_RIGHT:
    PUSH R2
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_STR_CONCAT_DONE
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_CONCAT_DONE
    LOAD_LW 4
    PUSH R2
    ADD_I32
    POP R3
    LBR_MOV1 R0 R3
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    RLB_MOV1 R0 R3
    INC R1
    INC R2
    JMP E_STR_CONCAT_RIGHT
    E_STR_CONCAT_DONE:
    PUSH R1
    STORE_LW 6
  }

  written = written;
  return written;
}

function int unicode_codepoint_count(int source_off, int source_len) {
  int count = 0;

  EPA {
    SET_R 1 0
    SET_R 2 0
    E_STR_CP_LOOP:
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_CP_DONE
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 128
    LT_I32
    POP R0
    JNZ E_STR_CP_ADD
    PUSH R3
    PUSH 192
    GE_I32
    POP R0
    JZ E_STR_CP_NEXT
    E_STR_CP_ADD:
    INC R2
    E_STR_CP_NEXT:
    INC R1
    JMP E_STR_CP_LOOP
    E_STR_CP_DONE:
    PUSH R2
    STORE_LW 2
  }

  count = count;
  return count;
}

function int i32_decimal_len(int value) {
  int len = 1;

  EPA {
    LOAD_LW 0
    POP R1
    SET_R 2 1
    PUSH R1
    PUSH 0
    LT_I32
    POP R0
    JZ E_STR_I32_LEN_ABS
    INC R2
    SET_R 3 0
    PUSH R3
    PUSH R1
    SUB_I32
    POP R1
    E_STR_I32_LEN_ABS:
    E_STR_I32_LEN_LOOP:
    PUSH R1
    PUSH 10
    GE_I32
    POP R0
    JZ E_STR_I32_LEN_DONE
    PUSH R1
    PUSH 10
    DIV_I32
    POP R1
    INC R2
    JMP E_STR_I32_LEN_LOOP
    E_STR_I32_LEN_DONE:
    PUSH R2
    STORE_LW 1
  }

  len = len;
  return len;
}

function int i32_decimal_into(int out_off, int out_cap, int value) {
  int written = 0;
  int needed = i32_decimal_len(value);
  int current = 0;
  int first_digit = 0;
  int quotient = 0;

  EPA {
    LOAD_LW 4
    LOAD_LW 1
    LE_I32
    POP R0
    JZ E_STR_I32_WRITE_DONE
    LOAD_LW 2
    POP R1
    SET_R 2 0
    PUSH R1
    PUSH 0
    LT_I32
    POP R0
    JZ E_STR_I32_WRITE_ABS
    SET_R 2 1
    SET_R 3 0
    PUSH R3
    PUSH R1
    SUB_I32
    POP R1
    SET_R 3 45
    LOAD_LW 0
    POP R0
    RLB_MOV1 R3 R0
    E_STR_I32_WRITE_ABS:
    PUSH R1
    STORE_LW 5
    PUSH R2
    STORE_LW 6
    LOAD_LW 4
    PUSH 1
    SUB_I32
    POP R2
    E_STR_I32_WRITE_LOOP:
    PUSH R2
    LOAD_LW 6
    GE_I32
    POP R0
    JZ E_STR_I32_WRITE_OK
    LOAD_LW 5
    PUSH 10
    DIV_I32
    STORE_LW 7
    LOAD_LW 5
    LOAD_LW 7
    PUSH 10
    MUL_I32
    SUB_I32
    PUSH 48
    ADD_I32
    POP R1
    LOAD_LW 0
    PUSH R2
    ADD_I32
    POP R3
    RLB_MOV1 R1 R3
    LOAD_LW 7
    STORE_LW 5
    PUSH R2
    LOAD_LW 6
    EQ_I32
    POP R0
    JNZ E_STR_I32_WRITE_OK
    DEC R2
    JMP E_STR_I32_WRITE_LOOP
    E_STR_I32_WRITE_OK:
    LOAD_LW 4
    STORE_LW 3
    E_STR_I32_WRITE_DONE:
  }

  needed = needed;
  current = current;
  first_digit = first_digit;
  quotient = quotient;
  written = written;
  return written;
}

function int unicode_format_len(int format_off, int format_len, ...) {
  int argc = vararg_count();
  int fmt_i = 0;
  int arg_i = 0;
  int len = 0;

  EPA {
    E_STR_FMT_LEN_LOOP:
    LOAD_LW 5
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_FMT_LEN_DONE
    LOAD_LW 0
    LOAD_LW 5
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 123
    EQ_I32
    POP R0
    JZ E_STR_FMT_LEN_LITERAL
    LOAD_LW 5
    PUSH 1
    ADD_I32
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_FMT_LEN_LITERAL
    LOAD_LW 0
    LOAD_LW 5
    PUSH 1
    ADD_I32
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 125
    EQ_I32
    POP R0
    JZ E_STR_FMT_LEN_LITERAL
    LOAD_LW 6
    LOAD_LW 4
    LT_I32
    POP R0
    JZ E_STR_FMT_LEN_LITERAL
    LOAD_LW 6
    PUSH 4
    MUL_I32
    LOAD_LW 2
    ADD_I32
    POP R3
    LBR_MOV4 R1 R3
    SET_R 2 1
    PUSH R1
    PUSH 0
    LT_I32
    POP R0
    JZ E_STR_FMT_LEN_ARG_ABS
    INC R2
    SET_R 3 0
    PUSH R3
    PUSH R1
    SUB_I32
    POP R1
    E_STR_FMT_LEN_ARG_ABS:
    E_STR_FMT_LEN_ARG_LOOP:
    PUSH R1
    PUSH 10
    GE_I32
    POP R0
    JZ E_STR_FMT_LEN_ARG_DONE
    PUSH R1
    PUSH 10
    DIV_I32
    POP R1
    INC R2
    JMP E_STR_FMT_LEN_ARG_LOOP
    E_STR_FMT_LEN_ARG_DONE:
    LOAD_LW 7
    PUSH R2
    ADD_I32
    STORE_LW 7
    LOAD_LW 6
    PUSH 1
    ADD_I32
    STORE_LW 6
    LOAD_LW 5
    PUSH 2
    ADD_I32
    STORE_LW 5
    JMP E_STR_FMT_LEN_LOOP
    E_STR_FMT_LEN_LITERAL:
    LOAD_LW 7
    PUSH 1
    ADD_I32
    STORE_LW 7
    LOAD_LW 5
    PUSH 1
    ADD_I32
    STORE_LW 5
    JMP E_STR_FMT_LEN_LOOP
    E_STR_FMT_LEN_DONE:
  }

  argc = argc;
  fmt_i = fmt_i;
  arg_i = arg_i;
  len = len;
  return len;
}

function int unicode_format_into(
  int out_off,
  int out_cap,
  int format_off,
  int format_len,
  ...
) {
  int written = 0;
  int current = 0;
  int needed = 0;
  int first_digit = 0;
  int quotient = 0;
  int fmt_i = 0;
  int arg_i = 0;

  EPA {
    E_STR_FMT_INTO_LOOP:
    LOAD_LW 11
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_STR_FMT_INTO_DONE
    LOAD_LW 6
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_STR_FMT_INTO_DONE
    LOAD_LW 2
    LOAD_LW 11
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 123
    EQ_I32
    POP R0
    JZ E_STR_FMT_INTO_LITERAL
    LOAD_LW 11
    PUSH 1
    ADD_I32
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_STR_FMT_INTO_LITERAL
    LOAD_LW 2
    LOAD_LW 11
    PUSH 1
    ADD_I32
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 125
    EQ_I32
    POP R0
    JZ E_STR_FMT_INTO_LITERAL
    LOAD_LW 12
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_STR_FMT_INTO_LITERAL
    LOAD_LW 12
    PUSH 4
    MUL_I32
    LOAD_LW 4
    ADD_I32
    POP R3
    LBR_MOV4 R1 R3
    SET_R 2 1
    SET_R 3 0
    PUSH R1
    PUSH 0
    LT_I32
    POP R0
    JZ E_STR_FMT_INTO_ARG_ABS
    SET_R 2 2
    SET_R 3 1
    SET_R 0 0
    PUSH R0
    PUSH R1
    SUB_I32
    POP R1
    E_STR_FMT_INTO_ARG_ABS:
    PUSH R1
    STORE_LW 7
    PUSH R2
    STORE_LW 8
    PUSH R3
    STORE_LW 9
    E_STR_FMT_INTO_ARG_LEN_LOOP:
    PUSH R1
    PUSH 10
    GE_I32
    POP R0
    JZ E_STR_FMT_INTO_ARG_LEN_DONE
    PUSH R1
    PUSH 10
    DIV_I32
    POP R1
    LOAD_LW 8
    PUSH 1
    ADD_I32
    STORE_LW 8
    JMP E_STR_FMT_INTO_ARG_LEN_LOOP
    E_STR_FMT_INTO_ARG_LEN_DONE:
    LOAD_LW 6
    LOAD_LW 8
    ADD_I32
    LOAD_LW 1
    LE_I32
    POP R0
    JZ E_STR_FMT_INTO_DONE
    LOAD_LW 9
    PUSH 1
    EQ_I32
    POP R0
    JZ E_STR_FMT_INTO_DIGITS
    SET_R 0 45
    LOAD_LW 0
    LOAD_LW 6
    ADD_I32
    POP R3
    RLB_MOV1 R0 R3
    E_STR_FMT_INTO_DIGITS:
    LOAD_LW 8
    PUSH 1
    SUB_I32
    POP R2
    E_STR_FMT_INTO_DIGIT_LOOP:
    PUSH R2
    LOAD_LW 9
    GE_I32
    POP R0
    JZ E_STR_FMT_INTO_DIGIT_DONE
    LOAD_LW 7
    PUSH 10
    DIV_I32
    STORE_LW 10
    LOAD_LW 7
    LOAD_LW 10
    PUSH 10
    MUL_I32
    SUB_I32
    PUSH 48
    ADD_I32
    POP R1
    LOAD_LW 0
    LOAD_LW 6
    ADD_I32
    PUSH R2
    ADD_I32
    POP R3
    RLB_MOV1 R1 R3
    LOAD_LW 10
    STORE_LW 7
    PUSH R2
    LOAD_LW 9
    EQ_I32
    POP R0
    JNZ E_STR_FMT_INTO_DIGIT_DONE
    DEC R2
    JMP E_STR_FMT_INTO_DIGIT_LOOP
    E_STR_FMT_INTO_DIGIT_DONE:
    LOAD_LW 6
    LOAD_LW 8
    ADD_I32
    STORE_LW 6
    LOAD_LW 12
    PUSH 1
    ADD_I32
    STORE_LW 12
    LOAD_LW 11
    PUSH 2
    ADD_I32
    STORE_LW 11
    JMP E_STR_FMT_INTO_LOOP
    E_STR_FMT_INTO_LITERAL:
    LOAD_LW 2
    LOAD_LW 11
    ADD_I32
    POP R3
    LBR_MOV1 R1 R3
    LOAD_LW 0
    LOAD_LW 6
    ADD_I32
    POP R3
    RLB_MOV1 R1 R3
    LOAD_LW 11
    PUSH 1
    ADD_I32
    STORE_LW 11
    LOAD_LW 6
    PUSH 1
    ADD_I32
    STORE_LW 6
    JMP E_STR_FMT_INTO_LOOP
    E_STR_FMT_INTO_DONE:
  }

  current = current;
  needed = needed;
  first_digit = first_digit;
  quotient = quotient;
  fmt_i = fmt_i;
  arg_i = arg_i;
  written = written;
  return written;
}

// Common EPA egress helpers.
//
// Include from E with:
//   #include "common/egress.em"
//
// These helpers deliberately replace the old FMT/LOG ISA opcodes. Formatting
// and human-readable logging are library/protocol behavior, not slim-core
// instructions.
//
// Egress frame v1 is intentionally small:
//   word0 kind
//   word1 payload_off
//   word2 payload_len
//   word3 payload_type
//
// The caller owns the payload bytes. A host/OS egress router interprets the
// frame according to kind and payload_type.

#include "common/string.em"

function int fmt_len(int format_off, int format_len, ...) {
  int argc = vararg_count();
  int fmt_i = 0;
  int arg_i = 0;
  int len = 0;

  EPA {
    E_EGRESS_FMT_LEN_LOOP:
    LOAD_LW 5
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_LEN_DONE
    LOAD_LW 0
    LOAD_LW 5
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 123
    EQ_I32
    POP R0
    JZ E_EGRESS_FMT_LEN_LITERAL
    LOAD_LW 5
    PUSH 1
    ADD_I32
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_LEN_LITERAL
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
    JZ E_EGRESS_FMT_LEN_LITERAL
    LOAD_LW 6
    LOAD_LW 4
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_LEN_LITERAL
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
    JZ E_EGRESS_FMT_LEN_ARG_ABS
    INC R2
    SET_R 3 0
    PUSH R3
    PUSH R1
    SUB_I32
    POP R1
    E_EGRESS_FMT_LEN_ARG_ABS:
    E_EGRESS_FMT_LEN_ARG_LOOP:
    PUSH R1
    PUSH 10
    GE_I32
    POP R0
    JZ E_EGRESS_FMT_LEN_ARG_DONE
    PUSH R1
    PUSH 10
    DIV_I32
    POP R1
    INC R2
    JMP E_EGRESS_FMT_LEN_ARG_LOOP
    E_EGRESS_FMT_LEN_ARG_DONE:
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
    JMP E_EGRESS_FMT_LEN_LOOP
    E_EGRESS_FMT_LEN_LITERAL:
    LOAD_LW 7
    PUSH 1
    ADD_I32
    STORE_LW 7
    LOAD_LW 5
    PUSH 1
    ADD_I32
    STORE_LW 5
    JMP E_EGRESS_FMT_LEN_LOOP
    E_EGRESS_FMT_LEN_DONE:
  }

  argc = argc;
  fmt_i = fmt_i;
  arg_i = arg_i;
  len = len;
  return len;
}

function int fmt_into(int out_off, int out_cap, int format_off, int format_len, ...) {
  int written = 0;
  int current = 0;
  int needed = 0;
  int first_digit = 0;
  int quotient = 0;
  int fmt_i = 0;
  int arg_i = 0;

  EPA {
    E_EGRESS_FMT_INTO_LOOP:
    LOAD_LW 11
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_DONE
    LOAD_LW 6
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_DONE
    LOAD_LW 2
    LOAD_LW 11
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH 123
    EQ_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_LITERAL
    LOAD_LW 11
    PUSH 1
    ADD_I32
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_LITERAL
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
    JZ E_EGRESS_FMT_INTO_LITERAL
    LOAD_LW 12
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_LITERAL
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
    JZ E_EGRESS_FMT_INTO_ARG_ABS
    SET_R 2 2
    SET_R 3 1
    SET_R 0 0
    PUSH R0
    PUSH R1
    SUB_I32
    POP R1
    E_EGRESS_FMT_INTO_ARG_ABS:
    PUSH R1
    STORE_LW 7
    PUSH R2
    STORE_LW 8
    PUSH R3
    STORE_LW 9
    E_EGRESS_FMT_INTO_ARG_LEN_LOOP:
    PUSH R1
    PUSH 10
    GE_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_ARG_LEN_DONE
    PUSH R1
    PUSH 10
    DIV_I32
    POP R1
    LOAD_LW 8
    PUSH 1
    ADD_I32
    STORE_LW 8
    JMP E_EGRESS_FMT_INTO_ARG_LEN_LOOP
    E_EGRESS_FMT_INTO_ARG_LEN_DONE:
    LOAD_LW 6
    LOAD_LW 8
    ADD_I32
    LOAD_LW 1
    LE_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_DONE
    LOAD_LW 9
    PUSH 1
    EQ_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_DIGITS
    SET_R 0 45
    LOAD_LW 0
    LOAD_LW 6
    ADD_I32
    POP R3
    RLB_MOV1 R0 R3
    E_EGRESS_FMT_INTO_DIGITS:
    LOAD_LW 8
    PUSH 1
    SUB_I32
    POP R2
    E_EGRESS_FMT_INTO_DIGIT_LOOP:
    PUSH R2
    LOAD_LW 9
    GE_I32
    POP R0
    JZ E_EGRESS_FMT_INTO_DIGIT_DONE
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
    JNZ E_EGRESS_FMT_INTO_DIGIT_DONE
    DEC R2
    JMP E_EGRESS_FMT_INTO_DIGIT_LOOP
    E_EGRESS_FMT_INTO_DIGIT_DONE:
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
    JMP E_EGRESS_FMT_INTO_LOOP
    E_EGRESS_FMT_INTO_LITERAL:
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
    JMP E_EGRESS_FMT_INTO_LOOP
    E_EGRESS_FMT_INTO_DONE:
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

function int log(int payload_off, int payload_len) {
  EPA {
    LOAD_LW 0
    POP R0
    LOAD_LW 1
    POP R1
    SET_R 2 1
    SET_R 3 1
    HOST_SIGNAL
  }
  return payload_off;
}

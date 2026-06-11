#ifndef ELARA_COMMON_BYTES_EM
#define ELARA_COMMON_BYTES_EM

// Local byte-span primitives for E.
//
// These functions operate on the worker/kernel local byte arena. They are the
// base layer for strings, binary protocols, egress frames, file records, and
// VM/IO packets. Offsets are byte offsets into local memory.

function int byte_load(int off) {
  int value = 0;

  EPA {
    LOAD_LW 0
    POP R1
    LBR_MOV1 R2 R1
    PUSH R2
    STORE_LW 1
  }

  value = value;
  return value;
}

function int byte_store(int off, int value) {
  EPA {
    LOAD_LW 0
    POP R1
    LOAD_LW 1
    POP R2
    RLB_MOV1 R2 R1
  }

  return value;
}

function int byte_zero(int off, int len) {
  int written = 0;

  EPA {
    SET_R 1 0
    SET_R 2 0
    E_BYTE_ZERO_LOOP:
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_ZERO_DONE
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    RLB_MOV1 R2 R3
    INC R1
    JMP E_BYTE_ZERO_LOOP
    E_BYTE_ZERO_DONE:
    PUSH R1
    STORE_LW 2
  }

  written = written;
  return written;
}

function int byte_fill(int off, int len, int value) {
  int written = 0;

  EPA {
    SET_R 1 0
    LOAD_LW 2
    POP R2
    E_BYTE_FILL_LOOP:
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_FILL_DONE
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    RLB_MOV1 R2 R3
    INC R1
    JMP E_BYTE_FILL_LOOP
    E_BYTE_FILL_DONE:
    PUSH R1
    STORE_LW 3
  }

  written = written;
  return written;
}

function int byte_copy(int dst_off, int dst_cap, int src_off, int len) {
  int written = 0;
  int limit = 0;

  EPA {
    LOAD_LW 3
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_COPY_USE_CAP
    LOAD_LW 3
    STORE_LW 5
    JMP E_BYTE_COPY_READY
    E_BYTE_COPY_USE_CAP:
    LOAD_LW 1
    STORE_LW 5
    E_BYTE_COPY_READY:
    SET_R 1 0
    E_BYTE_COPY_LOOP:
    PUSH R1
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_BYTE_COPY_DONE
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
    JMP E_BYTE_COPY_LOOP
    E_BYTE_COPY_DONE:
    PUSH R1
    STORE_LW 4
  }

  limit = limit;
  written = written;
  return written;
}

function int byte_move(int dst_off, int dst_cap, int src_off, int len) {
  int written = 0;
  int limit = 0;
  int src_end = 0;

  EPA {
    LOAD_LW 3
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_MOVE_USE_CAP
    LOAD_LW 3
    STORE_LW 5
    JMP E_BYTE_MOVE_LIMIT_READY
    E_BYTE_MOVE_USE_CAP:
    LOAD_LW 1
    STORE_LW 5
    E_BYTE_MOVE_LIMIT_READY:
    LOAD_LW 2
    LOAD_LW 5
    ADD_I32
    STORE_LW 6
    LOAD_LW 2
    LOAD_LW 0
    LT_I32
    POP R0
    JZ E_BYTE_MOVE_FORWARD
    LOAD_LW 0
    LOAD_LW 6
    LT_I32
    POP R0
    JZ E_BYTE_MOVE_FORWARD
    LOAD_LW 5
    POP R1
    E_BYTE_MOVE_BACK_LOOP:
    PUSH R1
    PUSH 0
    GT_I32
    POP R0
    JZ E_BYTE_MOVE_DONE
    DEC R1
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
    JMP E_BYTE_MOVE_BACK_LOOP
    E_BYTE_MOVE_FORWARD:
    SET_R 1 0
    E_BYTE_MOVE_FWD_LOOP:
    PUSH R1
    LOAD_LW 5
    LT_I32
    POP R0
    JZ E_BYTE_MOVE_DONE
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
    JMP E_BYTE_MOVE_FWD_LOOP
    E_BYTE_MOVE_DONE:
    LOAD_LW 5
    STORE_LW 4
  }

  src_end = src_end;
  limit = limit;
  written = written;
  return written;
}

function int byte_compare(int left_off, int left_len, int right_off, int right_len) {
  int result = 0;
  int i = 0;
  int limit = 0;

  EPA {
    LOAD_LW 1
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_BYTE_CMP_USE_RIGHT_LEN
    LOAD_LW 1
    STORE_LW 6
    JMP E_BYTE_CMP_LIMIT_READY
    E_BYTE_CMP_USE_RIGHT_LEN:
    LOAD_LW 3
    STORE_LW 6
    E_BYTE_CMP_LIMIT_READY:
    SET_R 1 0
    E_BYTE_CMP_LOOP:
    PUSH R1
    LOAD_LW 6
    LT_I32
    POP R0
    JZ E_BYTE_CMP_LENGTHS
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R2
    LBR_MOV1 R2 R2
    LOAD_LW 2
    PUSH R1
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R2
    PUSH R3
    EQ_I32
    POP R0
    JNZ E_BYTE_CMP_NEXT
    PUSH R2
    PUSH R3
    LT_I32
    POP R0
    JZ E_BYTE_CMP_GREATER
    PUSH 0
    PUSH 1
    SUB_I32
    STORE_LW 4
    JMP E_BYTE_CMP_DONE
    E_BYTE_CMP_GREATER:
    PUSH 1
    STORE_LW 4
    JMP E_BYTE_CMP_DONE
    E_BYTE_CMP_NEXT:
    INC R1
    JMP E_BYTE_CMP_LOOP
    E_BYTE_CMP_LENGTHS:
    LOAD_LW 1
    LOAD_LW 3
    EQ_I32
    POP R0
    JNZ E_BYTE_CMP_EQUAL
    LOAD_LW 1
    LOAD_LW 3
    LT_I32
    POP R0
    JZ E_BYTE_CMP_LEN_GREATER
    PUSH 0
    PUSH 1
    SUB_I32
    STORE_LW 4
    JMP E_BYTE_CMP_DONE
    E_BYTE_CMP_LEN_GREATER:
    PUSH 1
    STORE_LW 4
    JMP E_BYTE_CMP_DONE
    E_BYTE_CMP_EQUAL:
    PUSH 0
    STORE_LW 4
    E_BYTE_CMP_DONE:
  }

  limit = limit;
  i = i;
  result = result;
  return result;
}

function int byte_equal(int left_off, int left_len, int right_off, int right_len) {
  int cmp = byte_compare(left_off, left_len, right_off, right_len);
  if (cmp == 0) {
    return 1;
  }
  return 0;
}

function int byte_find(int off, int len, int value) {
  int index = 0;

  EPA {
    PUSH 0
    PUSH 1
    SUB_I32
    STORE_LW 3
    SET_R 1 0
    LOAD_LW 2
    POP R2
    E_BYTE_FIND_LOOP:
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_FIND_DONE
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R3
    LBR_MOV1 R3 R3
    PUSH R3
    PUSH R2
    EQ_I32
    POP R0
    JNZ E_BYTE_FIND_HIT
    INC R1
    JMP E_BYTE_FIND_LOOP
    E_BYTE_FIND_HIT:
    PUSH R1
    STORE_LW 3
    E_BYTE_FIND_DONE:
  }

  index = index;
  return index;
}

function int byte_count(int off, int len, int value) {
  int count = 0;

  EPA {
    SET_R 1 0
    LOAD_LW 2
    POP R3
    E_BYTE_COUNT_LOOP:
    PUSH R1
    LOAD_LW 1
    LT_I32
    POP R0
    JZ E_BYTE_COUNT_DONE
    LOAD_LW 0
    PUSH R1
    ADD_I32
    POP R2
    LBR_MOV1 R2 R2
    PUSH R2
    PUSH R3
    EQ_I32
    POP R0
    JZ E_BYTE_COUNT_NEXT
    LOAD_LW 3
    PUSH 1
    ADD_I32
    STORE_LW 3
    E_BYTE_COUNT_NEXT:
    INC R1
    JMP E_BYTE_COUNT_LOOP
    E_BYTE_COUNT_DONE:
  }

  count = count;
  return count;
}

function int bit_and_i32(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    AND_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int bit_or_i32(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    OR_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int bit_xor_i32(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    XOR_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int bit_not_i32(int input) {
  int value = 0;

  EPA {
    LOAD_LW 0
    NOT_I32
    STORE_LW 1
  }

  value = value;
  return value;
}

function int byte_and(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    AND_I32
    PUSH 255
    AND_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int byte_or(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    OR_I32
    PUSH 255
    AND_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int byte_xor(int left, int right) {
  int value = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    XOR_I32
    PUSH 255
    AND_I32
    STORE_LW 2
  }

  value = value;
  return value;
}

function int byte_not(int input) {
  int value = 0;

  EPA {
    LOAD_LW 0
    NOT_I32
    PUSH 255
    AND_I32
    STORE_LW 1
  }

  value = value;
  return value;
}

function int byte_rol(int value, int count) {
  int rotated = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    ROL_BYTE
    STORE_LW 2
  }

  rotated = rotated;
  return rotated;
}

function int byte_ror(int value, int count) {
  int rotated = 0;

  EPA {
    LOAD_LW 0
    LOAD_LW 1
    ROR_BYTE
    STORE_LW 2
  }

  rotated = rotated;
  return rotated;
}

function int u32_load_le(int off) {
  int value = 0;

  EPA {
    LOAD_LW 0
    POP R1
    LBR_MOV4 R2 R1
    PUSH R2
    STORE_LW 1
  }

  value = value;
  return value;
}

function int u16_load_le(int off) {
  int lo = byte_load(off);
  int hi = byte_load(off + 1);
  return byte_and(lo, 255) + (byte_and(hi, 255) * 256);
}

function int u32_store_le(int off, int value) {
  EPA {
    LOAD_LW 0
    POP R1
    LOAD_LW 1
    POP R2
    RLB_MOV4 R2 R1
  }

  return value;
}

#endif

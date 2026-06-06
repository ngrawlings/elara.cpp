#include "ctest.h"
#include "epa_asm_compiler.h"
#include "epa_program_loader.h"
#include "opcodes/epa_opcode_values.h"

#include <stdlib.h>

CTEST(test_f32_opcode_values_fill_slim_core_tail)
{
    ASSERT_EQ((unsigned)EPA_OP_PUSH_F32, 111u);
    ASSERT_EQ((unsigned)EPA_OP_ADD_F32, 112u);
    ASSERT_EQ((unsigned)EPA_OP_F32_TO_U32, 127u);
    ASSERT_EQ((unsigned)EPA_OP_RESERVED_MIN, 128u);
    ASSERT_TRUE(EPA_OP_IS_COMMON(EPA_OP_F32_TO_U32));
    ASSERT_TRUE(!EPA_OP_IS_COMMON(128u));
    return 0;
}

CTEST(test_f32_assembler_encodes_core_float_ops)
{
    const char *src =
        "ENTRY_START 1 0 0 0\n"
        "  PUSH_F32 1.5\n"
        "  PUSH_F32 2.25\n"
        "  ADD_F32\n"
        "  SQRT_F32\n"
        "  I32_TO_F32\n"
        "  F32_TO_U32\n"
        "ENTRY_END\n";
    char err[EPA_MAX_ERR];
    size_t blob_len = 0u;
    uint8_t *blob = epa_asm_compile_src(src, &blob_len, err);
    EpaProgramDesc prog;
    const uint8_t *code = NULL;
    size_t code_len = 0u;

    ASSERT_TRUE(blob != NULL);
    ASSERT_TRUE(epa_program_parse(&prog, blob, blob_len, err));
    ASSERT_TRUE(epa_prog_resolve(&prog, 0u, 1u, &code, &code_len));
    ASSERT_TRUE(code_len >= 14u);
    ASSERT_EQ((unsigned)code[0], (unsigned)EPA_OP_PUSH_F32);
    ASSERT_EQ((unsigned)code[1], 0x00u);
    ASSERT_EQ((unsigned)code[2], 0x00u);
    ASSERT_EQ((unsigned)code[3], 0xc0u);
    ASSERT_EQ((unsigned)code[4], 0x3fu);
    ASSERT_EQ((unsigned)code[5], (unsigned)EPA_OP_PUSH_F32);
    ASSERT_EQ((unsigned)code[10], (unsigned)EPA_OP_ADD_F32);
    ASSERT_EQ((unsigned)code[11], (unsigned)EPA_OP_SQRT_F32);
    ASSERT_EQ((unsigned)code[12], (unsigned)EPA_OP_I32_TO_F32);
    ASSERT_EQ((unsigned)code[13], (unsigned)EPA_OP_F32_TO_U32);

    epa_program_free(&prog);
    free(blob);
    return 0;
}

void ctest_register_test_f32_opcodes(void)
{
    const char *F = "test_f32_opcodes.c";
    REG(F, test_f32_opcode_values_fill_slim_core_tail);
    REG(F, test_f32_assembler_encodes_core_float_ops);
}

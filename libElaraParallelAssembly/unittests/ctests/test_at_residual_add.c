// unittests/ctests/test_at_residual_add.c
#include "ctest.h"

#include "atomic_tasks/at_residual_add.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

static int feq(float a, float b) {
    return fabsf(a - b) < 1e-6f;
}

CTEST(test_at_residual_add_out_of_place)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t count = 32;

    const size_t bytes =
        sizeof(AtResidualAddHdr_v1) +
        (size_t)count * sizeof(float) +   // X
        (size_t)count * sizeof(float) +   // R
        (size_t)count * sizeof(float);    // OUT

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtResidualAddHdr_v1 *hdr = (AtResidualAddHdr_v1 *)p;
    hdr->flags = 0;
    hdr->count = count;
    hdr->rsv0 = hdr->rsv1 = 0;
    p += sizeof(*hdr);

    float *X   = (float *)p; p += (size_t)count * sizeof(float);
    float *R   = (float *)p; p += (size_t)count * sizeof(float);
    float *OUT = (float *)p;

    for (uint32_t i = 0; i < count; i++) {
        X[i] = (float)i * 0.5f;
        R[i] = (float)(1000 - (int)i) * 0.25f;
        OUT[i] = -999.0f;
    }

    ASSERT_TRUE(at_residual_add_run(ghs, h) == AT_RESID_OK);

    for (uint32_t i = 0; i < count; i++) {
        float expect = X[i] + R[i];
        ASSERT_TRUE(feq(OUT[i], expect));
        // Ensure inputs unchanged (out-of-place)
        ASSERT_TRUE(feq(X[i], (float)i * 0.5f));
        ASSERT_TRUE(feq(R[i], (float)(1000 - (int)i) * 0.25f));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_residual_add_inplace_x)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t count = 32;

    // No OUT region needed when inplace
    const size_t bytes =
        sizeof(AtResidualAddHdr_v1) +
        (size_t)count * sizeof(float) +   // X
        (size_t)count * sizeof(float);    // R

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtResidualAddHdr_v1 *hdr = (AtResidualAddHdr_v1 *)p;
    hdr->flags = AT_RESID_F_OUT_INPLACE_X;
    hdr->count = count;
    hdr->rsv0 = hdr->rsv1 = 0;
    p += sizeof(*hdr);

    float *X = (float *)p; p += (size_t)count * sizeof(float);
    float *R = (float *)p;

    float Xorig[32];
    float Rorig[32];

    for (uint32_t i = 0; i < count; i++) {
        X[i] = (float)i;
        R[i] = (float)(i * 2);
        Xorig[i] = X[i];
        Rorig[i] = R[i];
    }

    ASSERT_TRUE(at_residual_add_run(ghs, h) == AT_RESID_OK);

    for (uint32_t i = 0; i < count; i++) {
        float expect = Xorig[i] + Rorig[i];
        ASSERT_TRUE(feq(X[i], expect));   // X overwritten
        ASSERT_TRUE(feq(R[i], Rorig[i])); // R unchanged
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_residual_add_inplace_r)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t count = 32;

    const size_t bytes =
        sizeof(AtResidualAddHdr_v1) +
        (size_t)count * sizeof(float) +   // X
        (size_t)count * sizeof(float);    // R

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtResidualAddHdr_v1 *hdr = (AtResidualAddHdr_v1 *)p;
    hdr->flags = AT_RESID_F_OUT_INPLACE_R;
    hdr->count = count;
    hdr->rsv0 = hdr->rsv1 = 0;
    p += sizeof(*hdr);

    float *X = (float *)p; p += (size_t)count * sizeof(float);
    float *R = (float *)p;

    float Xorig[32];
    float Rorig[32];

    for (uint32_t i = 0; i < count; i++) {
        X[i] = (float)(-((int)i));
        R[i] = (float)(i + 1);
        Xorig[i] = X[i];
        Rorig[i] = R[i];
    }

    ASSERT_TRUE(at_residual_add_run(ghs, h) == AT_RESID_OK);

    for (uint32_t i = 0; i < count; i++) {
        float expect = Xorig[i] + Rorig[i];
        ASSERT_TRUE(feq(R[i], expect));   // R overwritten
        ASSERT_TRUE(feq(X[i], Xorig[i])); // X unchanged
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_residual_add_both_inplace_flags_fail)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t count = 4;

    const size_t bytes =
        sizeof(AtResidualAddHdr_v1) +
        (size_t)count * sizeof(float) +
        (size_t)count * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    AtResidualAddHdr_v1 *hdr = (AtResidualAddHdr_v1 *)base;
    hdr->flags = AT_RESID_F_OUT_INPLACE_X | AT_RESID_F_OUT_INPLACE_R;
    hdr->count = count;

    ASSERT_TRUE(at_residual_add_run(ghs, h) == AT_RESID_ERR);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_residual_add(void) {
    const char *F = "test_at_residual_add.c";
    REG(F, test_at_residual_add_out_of_place);
    REG(F, test_at_residual_add_inplace_x);
    REG(F, test_at_residual_add_inplace_r);
    REG(F, test_at_residual_add_both_inplace_flags_fail);
}

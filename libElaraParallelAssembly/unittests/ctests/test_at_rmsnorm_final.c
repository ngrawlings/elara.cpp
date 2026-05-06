// unittests/ctests/test_at_rmsnorm_final.c
#include "ctest.h"

#include "atomic_tasks/at_rmsnorm_final.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

static int feq(float a, float b, float tol) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= tol;
}

static void ref_rmsnorm_row(const float *x, const float *w, float *y, uint32_t d, float eps) {
    float sumsq = 0.0f;
    for (uint32_t i = 0; i < d; i++) sumsq += x[i] * x[i];
    float mean = sumsq / (float)d;
    float inv_rms = 1.0f / sqrtf(mean + eps);
    for (uint32_t i = 0; i < d; i++) y[i] = x[i] * inv_rms * w[i];
}

CTEST(test_at_rmsnorm_final_basic_inplace)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t d = 8;
    const uint32_t rows = 1;
    const float eps = 1e-5f;

    const size_t bytes =
        sizeof(AtRmsnormFinalHdr_v1) +
        (size_t)d * rows * sizeof(float) +   // x
        (size_t)d * sizeof(float);            // w

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtRmsnormFinalHdr_v1 *hdr = (AtRmsnormFinalHdr_v1*)p;
    p += sizeof(*hdr);

    float *x = (float*)p;
    p += (size_t)d * rows * sizeof(float);

    float *w = (float*)p;

    hdr->magic   = AT_RMSNORM_FINAL_MAGIC;
    hdr->version = 1;
    hdr->flags   = AT_RMSNORM_FINAL_F_INPLACE_X;
    hdr->eps     = eps;
    hdr->d_model = d;
    hdr->n_rows  = rows;
    hdr->x_off   = (uint32_t)((uint8_t*)x - (uint8_t*)base);
    hdr->w_off   = (uint32_t)((uint8_t*)w - (uint8_t*)base);
    hdr->y_off   = 0;

    // Fill x and w
    for (uint32_t i = 0; i < d; i++) {
        x[i] = (float)(i + 1);          // 1..8
        w[i] = 1.0f + 0.1f * (float)i;  // 1.0, 1.1, ...
    }

    float y_ref[8];
    ref_rmsnorm_row(x, w, y_ref, d, eps);

    ASSERT_TRUE(at_rmsnorm_final_run(ghs, h) == AT_RMSNORM_FINAL_OK);

    // inplace: x now contains y
    for (uint32_t i = 0; i < d; i++) {
        ASSERT_TRUE(feq(x[i], y_ref[i], 1e-5f));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_rmsnorm_final_multirow_inplace)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t d = 4;
    const uint32_t rows = 3;
    const float eps = 1e-6f;

    const size_t bytes =
        sizeof(AtRmsnormFinalHdr_v1) +
        (size_t)d * rows * sizeof(float) +  // x
        (size_t)d * sizeof(float);          // w

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtRmsnormFinalHdr_v1 *hdr = (AtRmsnormFinalHdr_v1*)p;
    p += sizeof(*hdr);

    float *x = (float*)p;
    p += (size_t)d * rows * sizeof(float);

    float *w = (float*)p;

    hdr->magic   = AT_RMSNORM_FINAL_MAGIC;
    hdr->version = 1;
    hdr->flags   = AT_RMSNORM_FINAL_F_INPLACE_X;
    hdr->eps     = eps;
    hdr->d_model = d;
    hdr->n_rows  = rows;
    hdr->x_off   = (uint32_t)((uint8_t*)x - (uint8_t*)base);
    hdr->w_off   = (uint32_t)((uint8_t*)w - (uint8_t*)base);

    // weights
    for (uint32_t i = 0; i < d; i++) w[i] = 1.0f;

    // rows: different scales
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t i = 0; i < d; i++) {
            x[r*d + i] = (float)(1 + (int)i) * (float)(r + 1);
        }
    }

    float ref[12];
    for (uint32_t r = 0; r < rows; r++) {
        ref_rmsnorm_row(&x[r*d], w, &ref[r*d], d, eps);
    }

    ASSERT_TRUE(at_rmsnorm_final_run(ghs, h) == AT_RMSNORM_FINAL_OK);

    for (uint32_t i = 0; i < d * rows; i++) {
        ASSERT_TRUE(feq(x[i], ref[i], 1e-5f));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_rmsnorm_final_out_of_place)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t d = 6;
    const uint32_t rows = 1;
    const float eps = 1e-5f;

    const size_t bytes =
        sizeof(AtRmsnormFinalHdr_v1) +
        (size_t)d * rows * sizeof(float) +  // x
        (size_t)d * sizeof(float) +         // w
        (size_t)d * rows * sizeof(float);   // y

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtRmsnormFinalHdr_v1 *hdr = (AtRmsnormFinalHdr_v1*)p;
    p += sizeof(*hdr);

    float *x = (float*)p;
    p += (size_t)d * sizeof(float);

    float *w = (float*)p;
    p += (size_t)d * sizeof(float);

    float *y = (float*)p;

    hdr->magic   = AT_RMSNORM_FINAL_MAGIC;
    hdr->version = 1;
    hdr->flags   = AT_RMSNORM_FINAL_F_OUT_OF_PLACE;
    hdr->eps     = eps;
    hdr->d_model = d;
    hdr->n_rows  = rows;
    hdr->x_off   = (uint32_t)((uint8_t*)x - (uint8_t*)base);
    hdr->w_off   = (uint32_t)((uint8_t*)w - (uint8_t*)base);
    hdr->y_off   = (uint32_t)((uint8_t*)y - (uint8_t*)base);

    for (uint32_t i = 0; i < d; i++) {
        x[i] = (float)(i - 3);    // -3..2
        w[i] = 0.5f + 0.05f*i;
        y[i] = 123.0f;            // poison
    }

    float y_ref[6];
    ref_rmsnorm_row(x, w, y_ref, d, eps);

    ASSERT_TRUE(at_rmsnorm_final_run(ghs, h) == AT_RMSNORM_FINAL_OK);

    // x unchanged, y updated
    for (uint32_t i = 0; i < d; i++) {
        ASSERT_TRUE(feq(y[i], y_ref[i], 1e-5f));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_rmsnorm_final_bad_bounds_fails)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t d = 4;
    const uint32_t rows = 1;

    const size_t bytes =
        sizeof(AtRmsnormFinalHdr_v1) +
        (size_t)d * sizeof(float) + // x
        (size_t)d * sizeof(float);  // w

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtRmsnormFinalHdr_v1 *hdr = (AtRmsnormFinalHdr_v1*)p;
    p += sizeof(*hdr);

    float *x = (float*)p;
    p += (size_t)d * sizeof(float);
    float *w = (float*)p;

    hdr->magic   = AT_RMSNORM_FINAL_MAGIC;
    hdr->version = 1;
    hdr->flags   = AT_RMSNORM_FINAL_F_INPLACE_X;
    hdr->eps     = 1e-5f;
    hdr->d_model = d;
    hdr->n_rows  = rows;

    // Intentionally invalid offset (past end)
    hdr->x_off   = (uint32_t)bytes + 16;
    hdr->w_off   = (uint32_t)((uint8_t*)w - (uint8_t*)base);

    // Fill to avoid UB if mistakenly read
    for (uint32_t i = 0; i < d; i++) { x[i] = 1.0f; w[i] = 1.0f; }

    int rc = at_rmsnorm_final_run(ghs, h);
    ASSERT_TRUE(rc == AT_RMSNORM_FINAL_ERR_BOUNDS);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_rmsnorm_final(void) {
    const char *F = "test_at_rmsnorm_final.c";
    REG(F, test_at_rmsnorm_final_basic_inplace);
    REG(F, test_at_rmsnorm_final_multirow_inplace);
    REG(F, test_at_rmsnorm_final_out_of_place);
    REG(F, test_at_rmsnorm_final_bad_bounds_fails);
}

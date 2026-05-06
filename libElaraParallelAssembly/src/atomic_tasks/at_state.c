// src/atomic_tasks/at_state.c
#include "at_state.h"

#include <string.h>
#include <stdio.h>

AtState g_at_state;

int at_state_publish_model(uint32_t model_id, const AtModelView *view, char err[256])
{
    if (err) err[0] = 0;

    if (!view) {
        if (err) snprintf(err, 256, "at_state_publish_model: view is NULL");
        return -1;
    }

    if (model_id >= AT_MAX_MODELS) {
        if (err) snprintf(err, 256,
                          "at_state_publish_model: model_id %u out of range (max %u)",
                          (unsigned)model_id, (unsigned)AT_MAX_MODELS);
        return -1;
    }

    // Basic structural validity
    if (view->n_layers == 0 || view->layers == NULL) {
        if (err) snprintf(err, 256, "at_state_publish_model: invalid n_layers/layers");
        return -1;
    }

    // Required global weights
    if (view->tok_emb == NULL || view->final_rms_w == NULL) {
        if (err) snprintf(err, 256, "at_state_publish_model: missing global weights");
        return -1;
    }

    // lm_head can be tied to tok_emb; still must be non-null
    if (view->lm_head_w == NULL) {
        if (err) snprintf(err, 256, "at_state_publish_model: missing lm_head_w");
        return -1;
    }

    // Shallow copy: backing store must outlive this publish.
    g_at_state.models[model_id].view  = *view;
    g_at_state.models[model_id].valid = 1;

    return 0;
}

const AtModelView *at_state_get_model(uint32_t model_id)
{
    if (model_id >= AT_MAX_MODELS) return NULL;
    if (!g_at_state.models[model_id].valid) return NULL;
    return &g_at_state.models[model_id].view;
}

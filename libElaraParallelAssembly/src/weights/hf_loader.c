// src/weights/hf_loader.c
#include "hf_loader.h"
#include "hf_tensor_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// hf_loader.c (private)

typedef struct {
    char    *name;          // malloc'd
    uint32_t dtype;         // your AtTensorView dtype enum
    uint32_t rank;
    uint32_t dims[8];
    uint64_t data_start;    // absolute file offset (bytes from mmap base)
    uint64_t data_end;      // absolute file offset (exclusive)
} HfStTensor;

typedef struct {
    char     path[512];
    int      fd;
    uint64_t size;
    uint8_t *base;          // mmap base

    HfStTensor *tensors;    // parsed header table
    uint32_t    ntensors;
} HfStFile;

typedef struct {
    // simple shard cache: open-on-demand, kept alive for loader lifetime
    HfStFile  *files;
    uint32_t   nfiles;
    uint32_t   cap;
} HfStShardCache;

typedef struct {
    int ready;              // set once index parsed / store initialized
    int sharded;            // 0=single model.safetensors, 1=sharded via index.json

    // For single-file mode
    HfStFile single;

    // For sharded mode (index.json + model-xxxxx-of-xxxxx.safetensors)
    HfStShardCache shards;
} HfSafetensorsStore;

struct HfLoader {
    char model_dir[512];
    HfBackend backend;

    // parsed config.json
    Phi3Config cfg;

    // sharded layout helper (tensor_name -> shard filename + offsets)
    // (can be NULL if single-file)
    HfTensorIndex *index;

    // backend-owned state
    union {
        HfSafetensorsStore st;
        // Future: PyTorch bin store
        // HfPytorchStore pt;
    } u;
};

static int st_open_file(HfStFile *f, const char *path, char err[256]);

static void st_file_init(HfStFile *f) {
    memset(f, 0, sizeof(*f));
    f->fd = -1;
}

static void st_file_destroy(HfStFile *f) {
    if (!f) return;
    if (f->base && f->size) munmap(f->base, (size_t)f->size);
    if (f->fd >= 0) close(f->fd);
    for (uint32_t i = 0; i < f->ntensors; i++) free(f->tensors[i].name);
    free(f->tensors);
    st_file_init(f);
}

static uint64_t rd_u64_le(const uint8_t *p) {
    return ((uint64_t)p[0])       |
           ((uint64_t)p[1] << 8)  |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

/* ------------------------- small path helpers ------------------------- */

static void hf_strip_trailing_slashes(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/') {
        s[n - 1] = 0;
        n--;
    }
}

static void hf_path_join(char *dst, size_t dst_sz, const char *a, const char *b) {
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (!a) a = "";
    if (!b) b = "";

    // remove trailing slashes from a, leading slashes from b
    char tmpa[1024];
    snprintf(tmpa, sizeof(tmpa), "%s", a);
    hf_strip_trailing_slashes(tmpa);

    while (*b == '/') b++;

    if (!tmpa[0]) {
        snprintf(dst, dst_sz, "%s", b);
        return;
    }
    if (!b[0]) {
        snprintf(dst, dst_sz, "%s", tmpa);
        return;
    }
    snprintf(dst, dst_sz, "%s/%s", tmpa, b);
}

/* ------------------------- file helpers ------------------------- */

static int hf_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int hf_endswith(const char *s, const char *suffix) {
    size_t n = s ? strlen(s) : 0;
    size_t m = suffix ? strlen(suffix) : 0;
    if (!s || !suffix || n < m) return 0;
    return memcmp(s + (n - m), suffix, m) == 0;
}

static void hf_dirname(char *dst, size_t dst_sz, const char *path) {
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;

    if (!path || !path[0]) {
        snprintf(dst, dst_sz, ".");
        return;
    }

    // copy
    snprintf(dst, dst_sz, "%s", path);
    hf_strip_trailing_slashes(dst);

    char *slash = strrchr(dst, '/');
    if (!slash) {
        snprintf(dst, dst_sz, ".");
        return;
    }
    if (slash == dst) {
        dst[1] = 0; // "/"
        return;
    }
    *slash = 0;
}



static int hf_read_entire_file(const char *path, char **out_buf, size_t *out_sz, char err[256]) {
    if (err) err[0] = 0;
    if (!path || !out_buf) {
        if (err) snprintf(err, 256, "hf_read_entire_file: invalid args");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err, 256, "hf_read_entire_file: could not open %s", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err) snprintf(err, 256, "hf_read_entire_file: fseek end failed");
        return -1;
    }

    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        if (err) snprintf(err, 256, "hf_read_entire_file: ftell failed");
        return -1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (err) snprintf(err, 256, "hf_read_entire_file: fseek set failed");
        return -1;
    }

    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        if (err) snprintf(err, 256, "hf_read_entire_file: OOM (%ld bytes)", n);
        return -1;
    }

    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);

    if (rd != (size_t)n) {
        free(buf);
        if (err) snprintf(err, 256, "hf_read_entire_file: short read");
        return -1;
    }

    buf[n] = 0;
    *out_buf = buf;
    if (out_sz) *out_sz = (size_t)n;
    return 0;
}

/* ------------------------- config.json mini-parser ------------------------- */
/*
 * NOTE: This is intentionally NOT a full JSON parser.
 * It is a robust-enough scanner for HF config.json numeric fields.
 */

static const char* hf_skip_ws(const char *p) {
    while (*p && (unsigned char)*p <= 32) p++;
    return p;
}

static const char* hf_find_quoted_key(const char *buf, const char *key) {
    // Finds `"key"` occurrences only.
    size_t klen = strlen(key);
    const char *p = buf;
    while ((p = strstr(p, key)) != NULL) {
        if (p > buf && p[-1] == '"' && p[klen] == '"') return p - 1; // points at first quote
        p += klen;
    }
    return NULL;
}

static int hf_json_find_number(const char *buf, const char *key, double *out) {
    const char *k = hf_find_quoted_key(buf, key);
    if (!k) return 0;

    const char *p = strchr(k, ':');
    if (!p) return 0;
    p++;
    p = hf_skip_ws(p);

    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return 0;

    *out = v;
    return 1;
}

static int hf_json_find_u32_any(const char *buf, const char **keys, uint32_t *out) {
    for (int i = 0; keys[i]; i++) {
        double v;
        if (hf_json_find_number(buf, keys[i], &v)) {
            if (v < 0.0) return 0;
            *out = (uint32_t)(v);
            return 1;
        }
    }
    return 0;
}

static int hf_json_find_f32_any(const char *buf, const char **keys, float *out) {
    for (int i = 0; keys[i]; i++) {
        double v;
        if (hf_json_find_number(buf, keys[i], &v)) {
            *out = (float)v;
            return 1;
        }
    }
    return 0;
}

int hf_read_config(Phi3Config *cfg, const char *model_dir, char err[256]) {
    if (err) err[0] = 0;
    if (!cfg || !model_dir) {
        if (err) snprintf(err, 256, "hf_read_config: invalid args");
        return -1;
    }

    char path[1024];
    hf_path_join(path, sizeof(path), model_dir, "config.json");

    char *buf = NULL;
    if (hf_read_entire_file(path, &buf, NULL, err) != 0) return -1;

    memset(cfg, 0, sizeof(*cfg));
    cfg->rope_theta = 10000.0f;        // default
    cfg->max_position_embeddings = 0;  // optional

    const char *K_layers[]   = { "num_hidden_layers", "n_layer", NULL };
    const char *K_hidden[]   = { "hidden_size", "n_embd", NULL };
    const char *K_heads[]    = { "num_attention_heads", "n_head", NULL };
    const char *K_vocab[]    = { "vocab_size", NULL };
    const char *K_mlp[]      = { "intermediate_size", "ffn_dim", NULL };
    const char *K_head_dim[] = { "head_dim", NULL };
    const char *K_rope[]     = { "rope_theta", NULL };
    const char *K_maxpos[]   = { "max_position_embeddings", NULL };

    int ok = 1;
    ok &= hf_json_find_u32_any(buf, K_layers, &cfg->n_layers);
    ok &= hf_json_find_u32_any(buf, K_hidden, &cfg->hidden_dim);
    ok &= hf_json_find_u32_any(buf, K_heads,  &cfg->n_heads);
    ok &= hf_json_find_u32_any(buf, K_vocab,  &cfg->vocab_size);

    // optional
    (void)hf_json_find_u32_any(buf, K_mlp, &cfg->mlp_dim);
    (void)hf_json_find_u32_any(buf, K_head_dim, &cfg->head_dim);
    (void)hf_json_find_f32_any(buf, K_rope, &cfg->rope_theta);
    (void)hf_json_find_u32_any(buf, K_maxpos, &cfg->max_position_embeddings);

    free(buf);

    if (!ok) {
        if (err) snprintf(err, 256,
            "hf_read_config: missing required fields in config.json "
            "(need num_hidden_layers/hidden_size/num_attention_heads/vocab_size)");
        return -1;
    }

    if (cfg->head_dim == 0) {
        if (cfg->n_heads == 0 || (cfg->hidden_dim % cfg->n_heads) != 0) {
            if (err) snprintf(err, 256,
                "hf_read_config: cannot derive head_dim (hidden_dim=%u n_heads=%u)",
                (unsigned)cfg->hidden_dim, (unsigned)cfg->n_heads);
            return -1;
        }
        cfg->head_dim = cfg->hidden_dim / cfg->n_heads;
    }

    return 0;
}

/* ------------------------- backend detection ------------------------- */

static void hf_normalize_model_dir(char out_dir[512], const char *path_or_dir) {
    // Accept dir OR .../config.json. Store normalized dir without trailing slash.
    if (!out_dir) return;
    out_dir[0] = 0;

    if (!path_or_dir || !path_or_dir[0]) {
        snprintf(out_dir, 512, ".");
        return;
    }

    if (hf_endswith(path_or_dir, "/config.json") || hf_endswith(path_or_dir, "config.json")) {
        hf_dirname(out_dir, 512, path_or_dir);
        hf_strip_trailing_slashes(out_dir);
        return;
    }

    snprintf(out_dir, 512, "%s", path_or_dir);
    hf_strip_trailing_slashes(out_dir);
}

static HfBackend hf_detect_backend(const char *model_dir) {
    char p[1024];

    // Safetensors (single)
    hf_path_join(p, sizeof(p), model_dir, "model.safetensors");
    if (hf_file_exists(p)) return HF_BACKEND_SAFETENSORS;

    // Safetensors (index sharded)
    hf_path_join(p, sizeof(p), model_dir, "model.safetensors.index.json");
    if (hf_file_exists(p)) return HF_BACKEND_SAFETENSORS;

    // Optional: common shard naming without index (cheap probe)
    hf_path_join(p, sizeof(p), model_dir, "model-00001-of-00002.safetensors");
    if (hf_file_exists(p)) return HF_BACKEND_SAFETENSORS;

    // PyTorch bin (single)
    hf_path_join(p, sizeof(p), model_dir, "pytorch_model.bin");
    if (hf_file_exists(p)) return HF_BACKEND_PYTORCH_BIN;

    // PyTorch bin (index sharded)
    hf_path_join(p, sizeof(p), model_dir, "pytorch_model.bin.index.json");
    if (hf_file_exists(p)) return HF_BACKEND_PYTORCH_BIN;

    // Optional: common shard naming without index (cheap probe)
    hf_path_join(p, sizeof(p), model_dir, "pytorch_model-00001-of-00002.bin");
    if (hf_file_exists(p)) return HF_BACKEND_PYTORCH_BIN;

    return HF_BACKEND_NONE;
}

static HfStFile* st_cache_get_or_open(HfSafetensorsStore *st, const char *path, char err[256]) {
    // search existing
    for (uint32_t i = 0; i < st->shards.nfiles; i++) {
        if (strcmp(st->shards.files[i].path, path) == 0) return &st->shards.files[i];
    }

    // grow array
    if (st->shards.nfiles == st->shards.cap) {
        uint32_t new_cap = st->shards.cap ? st->shards.cap * 2 : 8;
        HfStFile *nf = (HfStFile*)realloc(st->shards.files, (size_t)new_cap * sizeof(HfStFile));
        if (!nf) {
            if (err) snprintf(err, 256, "safetensors: OOM growing shard cache");
            return NULL;
        }
        // init new tail
        for (uint32_t i = st->shards.cap; i < new_cap; i++) st_file_init(&nf[i]);
        st->shards.files = nf;
        st->shards.cap = new_cap;
    }

    // open
    HfStFile *f = &st->shards.files[st->shards.nfiles++];
    st_file_init(f);

    if (st_open_file(f, path, err) != 0) {
        // rollback slot
        st->shards.nfiles--;
        st_file_destroy(f);
        return NULL;
    }

    return f;
}


/* ------------------------- public API ------------------------- */

static const HfStTensor* st_find_tensor(const HfStFile *f, const char *name);

int hf_open_model_dir(HfLoader **out, const char *model_path_or_dir, char err[256]) {
    if (err) err[0] = 0;
    if (!out || !model_path_or_dir) {
        if (err) snprintf(err, 256, "hf_open_model_dir: invalid args");
        return -1;
    }

    *out = NULL;

    HfLoader *L = (HfLoader*)calloc(1, sizeof(*L));
    if (!L) {
        if (err) snprintf(err, 256, "hf_open_model_dir: OOM");
        return -1;
    }

    hf_normalize_model_dir(L->model_dir, model_path_or_dir);

    // backend detection
    L->backend = hf_detect_backend(L->model_dir);
    if (L->backend == HF_BACKEND_NONE) {
        if (err) snprintf(err, 256,
            "hf_open_model_dir: could not detect model backend in %s "
            "(expected model.safetensors[.index.json] or pytorch_model.bin[.index.json])",
            L->model_dir);
        hf_close(L);
        return -1;
    }

    // config.json
    if (hf_read_config(&L->cfg, L->model_dir, err) != 0) {
        hf_close(L);
        return -1;
    }

    if (L->backend == HF_BACKEND_SAFETENSORS) {
        HfSafetensorsStore *st = &L->u.st;
        memset(st, 0, sizeof(*st));
        st_file_init(&st->single);

        char p_single[1024];
        char p_index[1024];
        hf_path_join(p_single, sizeof(p_single), L->model_dir, "model.safetensors");
        hf_path_join(p_index,  sizeof(p_index),  L->model_dir, "model.safetensors.index.json");

        if (hf_file_exists(p_single)) {
            // ---- single-file safetensors ----
            if (st_open_file(&st->single, p_single, err) != 0) {
                hf_close(L);
                return -1;
            }
            st->sharded = 0;
            st->ready   = 1;
        } else if (hf_file_exists(p_index)) {
        	char p_index[1024];
        	hf_path_join(p_index, sizeof(p_index), L->model_dir, "model.safetensors.index.json");

        	if (hf_index_load_json(&L->index, p_index, err) != 0) {
        	    hf_close(L);
        	    return -1;
        	}

        	L->u.st.sharded = 1;
        	L->u.st.ready   = 1;
        } else {
            snprintf(err, 256,
                     "hf_open_model_dir: safetensors backend: missing model.safetensors and model.safetensors.index.json");
            hf_close(L);
            return -1;
        }
    }

    *out = L;
    return 0;
}

void hf_close(HfLoader *L) {
    if (!L) return;

    if (L->index) {
        hf_index_destroy(L->index);
        L->index = NULL;
    }

    if (L->backend == HF_BACKEND_SAFETENSORS) {
        HfSafetensorsStore *st = &L->u.st;

        if (st->sharded) {
            // destroy shard cache
            for (uint32_t i = 0; i < st->shards.nfiles; i++) {
                st_file_destroy(&st->shards.files[i]);
            }
            free(st->shards.files);
            st->shards.files = NULL;
            st->shards.nfiles = st->shards.cap = 0;
        } else {
            // destroy single file
            st_file_destroy(&st->single);
        }

        memset(st, 0, sizeof(*st));
    }

    free(L);
}


const Phi3Config *hf_config(const HfLoader *L) {
    return L ? &L->cfg : NULL;
}

// In hf_loader.c

static const char *hf_backend_str(HfBackend b) {
    switch (b) {
        case HF_BACKEND_SAFETENSORS:  return "safetensors";
        case HF_BACKEND_PYTORCH_BIN:  return "pytorch_bin";
        default:                      return "none";
    }
}

/*
 * Backend-specific hooks (to be implemented as milestones land)
 *
 * safetensors:
 *   - open shard mmap(s)
 *   - parse header JSON (name -> dtype/shape/data_offsets)
 *   - return tensor view pointing into mmapped data
 *
 * pytorch bin:
 *   - parse archive (later milestone)
 */
static int hf_get_tensor_safetensors(const HfLoader *L,
                                     const char *name,
                                     AtTensorView *out_tv,
                                     char err[256])
{
    if (err) err[0] = 0;
    if (!L || !name || !out_tv) {
        if (err) snprintf(err, 256, "hf_get_tensor(safetensors): invalid args");
        return -1;
    }
    const HfSafetensorsStore *st = &L->u.st;
    if (!st->ready) {
        if (err) snprintf(err, 256, "hf_get_tensor(safetensors): store not initialized");
        return -1;
    }

    if (st->sharded) {
        // ask index where this tensor lives
    	char shard_rel[512];
    	if (hf_index_find_safetensors_shard(L->index, name, shard_rel, sizeof(shard_rel), err) != 0) return -1;

        char shard_path[1024];
        hf_path_join(shard_path, sizeof(shard_path), L->model_dir, shard_rel);

        HfStFile *sf = st_cache_get_or_open((HfSafetensorsStore*)st, shard_path, err);
        if (!sf) return -1;

        const HfStTensor *t = st_find_tensor(sf, name);
        if (!t) {
            if (err) snprintf(err, 256, "hf_get_tensor(safetensors): tensor not found in shard: %s", name);
            return -1;
        }

        memset(out_tv, 0, sizeof(*out_tv));
        out_tv->dtype  = t->dtype;
        out_tv->rank   = t->rank;
        for (uint32_t i = 0; i < t->rank && i < 8; i++) out_tv->dims[i] = t->dims[i];
        out_tv->data   = (void*)(sf->base + t->data_start);
        out_tv->nbytes = (uint64_t)(t->data_end - t->data_start);
        return 0;
    }

    const HfStTensor *t = st_find_tensor(&st->single, name);
    if (!t) {
        if (err) snprintf(err, 256, "hf_get_tensor(safetensors): tensor not found: %s", name);
        return -1;
    }

    memset(out_tv, 0, sizeof(*out_tv));
    out_tv->dtype  = t->dtype;
    out_tv->rank   = t->rank;
    for (uint32_t i = 0; i < t->rank && i < 8; i++) out_tv->dims[i] = t->dims[i];
    out_tv->data   = (void*)(st->single.base + t->data_start);
    out_tv->nbytes = (uint64_t)(t->data_end - t->data_start);
    return 0;
}

static int hf_list_tensors_safetensors(const HfLoader *L,
                                       hf_list_cb cb,
                                       void *user,
                                       char err[256])
{
    if (err) err[0] = 0;
    if (!L || !cb) {
        if (err) snprintf(err, 256, "hf_list_tensors(safetensors): invalid args");
        return -1;
    }
    const HfSafetensorsStore *st = &L->u.st;
    if (!st->ready) {
        if (err) snprintf(err, 256, "hf_list_tensors(safetensors): store not initialized");
        return -1;
    }
    if (st->sharded) {
    	return hf_index_list_names(L->index, cb, user, err);
    }

    for (uint32_t i = 0; i < st->single.ntensors; i++) {
        cb(user, st->single.tensors[i].name);
    }
    return 0;
}

static int hf_get_tensor_pytorch_bin(const HfLoader *L, const char *name,
                                     AtTensorView *out_tv, char err[256])
{
    (void)L; (void)name; (void)out_tv;
    if (err) snprintf(err, 256,
        "hf_get_tensor: pytorch .bin backend not implemented (deferred milestone)");
    return -1;
}

static int hf_list_tensors_pytorch_bin(const HfLoader *L, hf_list_cb cb, void *user,
                                       char err[256])
{
    (void)L; (void)cb; (void)user;
    if (err) snprintf(err, 256,
        "hf_list_tensors: pytorch .bin backend not implemented (deferred milestone)");
    return -1;
}

int hf_get_tensor(const HfLoader *L, const char *tensor_name,
                  AtTensorView *out_tv, char err[256])
{
    if (err) err[0] = 0;

    if (!L || !tensor_name || !tensor_name[0] || !out_tv) {
        if (err) snprintf(err, 256, "hf_get_tensor: invalid args");
        return -1;
    }

    memset(out_tv, 0, sizeof(*out_tv));

    switch (L->backend) {
        case HF_BACKEND_SAFETENSORS:
            return hf_get_tensor_safetensors(L, tensor_name, out_tv, err);

        case HF_BACKEND_PYTORCH_BIN:
            return hf_get_tensor_pytorch_bin(L, tensor_name, out_tv, err);

        default:
            if (err) snprintf(err, 256,
                "hf_get_tensor: loader backend is %s (invalid state)",
                hf_backend_str(L->backend));
            return -1;
    }
}

int hf_list_tensors(const HfLoader *L, hf_list_cb cb, void *user, char err[256])
{
    if (err) err[0] = 0;

    if (!L || !cb) {
        if (err) snprintf(err, 256, "hf_list_tensors: invalid args");
        return -1;
    }

    switch (L->backend) {
        case HF_BACKEND_SAFETENSORS:
            return hf_list_tensors_safetensors(L, cb, user, err);

        case HF_BACKEND_PYTORCH_BIN:
            return hf_list_tensors_pytorch_bin(L, cb, user, err);

        default:
            if (err) snprintf(err, 256,
                "hf_list_tensors: loader backend is %s (invalid state)",
                hf_backend_str(L->backend));
            return -1;
    }
}

static const char* js_skip_ws(const char *p, const char *end) {
    while (p < end && (unsigned char)*p <= 32) p++;
    return p;
}

static int js_expect(char c, const char **pp, const char *end) {
    const char *p = js_skip_ws(*pp, end);
    if (p >= end || *p != c) return 0;
    *pp = p + 1;
    return 1;
}

static int js_parse_string(const char **pp, const char *end, char **out_str) {
    const char *p = js_skip_ws(*pp, end);
    if (p >= end || *p != '"') return 0;
    p++;
    const char *s = p;

    // Minimal: safetensors header rarely escapes; handle \" and \\ anyway.
    char *buf = (char*)malloc((size_t)(end - s) + 1);
    if (!buf) return 0;
    size_t w = 0;

    while (p < end) {
        char ch = *p++;
        if (ch == '"') break;
        if (ch == '\\' && p < end) {
            char esc = *p++;
            if (esc == '"' || esc == '\\' || esc == '/') ch = esc;
            else if (esc == 'n') ch = '\n';
            else if (esc == 't') ch = '\t';
            else if (esc == 'r') ch = '\r';
            else ch = esc; // best-effort
        }
        buf[w++] = ch;
    }
    buf[w] = 0;

    if (p > end) { free(buf); return 0; }
    *out_str = buf;
    *pp = p;
    return 1;
}

static int js_parse_u64(const char **pp, const char *end, uint64_t *out) {
    const char *p = js_skip_ws(*pp, end);
    if (p >= end) return 0;

    // unsigned ints only (safetensors uses non-negative)
    uint64_t v = 0;
    int any = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        any = 1;
        uint64_t d = (uint64_t)(*p - '0');
        v = v * 10 + d;
        p++;
    }
    if (!any) return 0;
    *out = v;
    *pp = p;
    return 1;
}

static int js_skip_value(const char **pp, const char *end);

// skip array
static int js_skip_array(const char **pp, const char *end) {
    if (!js_expect('[', pp, end)) return 0;
    for (;;) {
        const char *p = js_skip_ws(*pp, end);
        if (p >= end) return 0;
        if (*p == ']') { *pp = p + 1; return 1; }
        if (!js_skip_value(pp, end)) return 0;
        p = js_skip_ws(*pp, end);
        if (p < end && *p == ',') { *pp = p + 1; continue; }
        if (p < end && *p == ']') { *pp = p + 1; return 1; }
        return 0;
    }
}

// skip object
static int js_skip_object(const char **pp, const char *end) {
    if (!js_expect('{', pp, end)) return 0;
    for (;;) {
        const char *p = js_skip_ws(*pp, end);
        if (p >= end) return 0;
        if (*p == '}') { *pp = p + 1; return 1; }

        char *k = NULL;
        if (!js_parse_string(pp, end, &k)) return 0;
        free(k);

        if (!js_expect(':', pp, end)) return 0;
        if (!js_skip_value(pp, end)) return 0;

        p = js_skip_ws(*pp, end);
        if (p < end && *p == ',') { *pp = p + 1; continue; }
        if (p < end && *p == '}') { *pp = p + 1; return 1; }
        return 0;
    }
}

static int js_skip_value(const char **pp, const char *end) {
    const char *p = js_skip_ws(*pp, end);
    if (p >= end) return 0;

    if (*p == '"') {
        char *tmp = NULL;
        int ok = js_parse_string(pp, end, &tmp);
        free(tmp);
        return ok;
    }
    if (*p == '{') return js_skip_object(pp, end);
    if (*p == '[') return js_skip_array(pp, end);

    // number / literals
    if ((*p >= '0' && *p <= '9') || *p == '-') {
        // skip number
        p++;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
        *pp = p;
        return 1;
    }

    if ((end - p) >= 4 && memcmp(p, "true", 4) == 0)  { *pp = p + 4; return 1; }
    if ((end - p) >= 5 && memcmp(p, "false",5) == 0)  { *pp = p + 5; return 1; }
    if ((end - p) >= 4 && memcmp(p, "null", 4) == 0)  { *pp = p + 4; return 1; }

    return 0;
}

static int st_dtype_from_str(const char *s, uint32_t *out_dtype) {
    // Adjust these to match your AtDType values.
    // Example:
    // AT_DTYPE_F32 = 1, AT_DTYPE_F16 = 2, AT_DTYPE_BF16 = 3
    if (!s || !out_dtype) return 0;

    if (strcmp(s, "F32") == 0) { *out_dtype = 1; return 1; }
    if (strcmp(s, "F16") == 0) { *out_dtype = 2; return 1; }
    if (strcmp(s, "BF16") == 0){ *out_dtype = 3; return 1; }
    // Phi-3 sometimes uses F16/BF16
    return 0;
}

static int st_parse_shape_u32(const char **pp, const char *end,
                              uint32_t dims[8], uint32_t *out_rank)
{
    if (!js_expect('[', pp, end)) return 0;
    uint32_t r = 0;

    for (;;) {
        const char *p = js_skip_ws(*pp, end);
        if (p >= end) return 0;
        if (*p == ']') { *pp = p + 1; break; }

        uint64_t v = 0;
        if (!js_parse_u64(pp, end, &v)) return 0;
        if (r < 8) dims[r] = (uint32_t)v;
        r++;

        p = js_skip_ws(*pp, end);
        if (p < end && *p == ',') { *pp = p + 1; continue; }
        if (p < end && *p == ']') { *pp = p + 1; break; }
        return 0;
    }

    if (r > 8) return 0; // v1 cap
    *out_rank = r;
    return 1;
}

static int st_parse_offsets(const char **pp, const char *end, uint64_t *a, uint64_t *b) {
    if (!js_expect('[', pp, end)) return 0;
    if (!js_parse_u64(pp, end, a)) return 0;
    if (!js_expect(',', pp, end)) return 0;
    if (!js_parse_u64(pp, end, b)) return 0;
    if (!js_expect(']', pp, end)) return 0;
    return 1;
}

static int st_parse_header_json(const char *json, uint64_t json_len,
                                HfStTensor **out_tensors, uint32_t *out_count,
                                char err[256])
{
    if (err) err[0] = 0;
    *out_tensors = NULL;
    *out_count = 0;

    const char *p = json;
    const char *end = json + (size_t)json_len;

    if (!js_expect('{', &p, end)) {
        if (err) snprintf(err, 256, "safetensors: header is not a JSON object");
        return -1;
    }

    // dynamic array
    uint32_t cap = 256;
    uint32_t n = 0;
    HfStTensor *ts = (HfStTensor*)calloc(cap, sizeof(HfStTensor));
    if (!ts) { if (err) snprintf(err, 256, "safetensors: OOM header table"); return -1; }

    for (;;) {
        p = js_skip_ws(p, end);
        if (p >= end) { free(ts); if (err) snprintf(err, 256, "safetensors: truncated header"); return -1; }
        if (*p == '}') { p++; break; }

        char *key = NULL;
        if (!js_parse_string(&p, end, &key)) { free(ts); if (err) snprintf(err, 256, "safetensors: bad key string"); return -1; }

        if (!js_expect(':', &p, end)) { free(key); free(ts); if (err) snprintf(err, 256, "safetensors: expected ':'"); return -1; }

        // "__metadata__": { ... } -> skip
        if (strcmp(key, "__metadata__") == 0) {
            free(key);
            if (!js_skip_value(&p, end)) { free(ts); if (err) snprintf(err, 256, "safetensors: bad __metadata__"); return -1; }
        } else {
            // value must be object with dtype/shape/data_offsets
            if (!js_expect('{', &p, end)) { free(key); free(ts); if (err) snprintf(err, 256, "safetensors: tensor entry not object"); return -1; }

            char *dtype_s = NULL;
            uint32_t rank = 0;
            uint32_t dims[8] = {0};
            uint64_t off_a = 0, off_b = 0;

            for (;;) {
                p = js_skip_ws(p, end);
                if (p >= end) { free(key); free(ts); if (err) snprintf(err, 256, "safetensors: truncated tensor obj"); return -1; }
                if (*p == '}') { p++; break; }

                char *k2 = NULL;
                if (!js_parse_string(&p, end, &k2)) { free(key); free(ts); if (err) snprintf(err, 256, "safetensors: bad field key"); return -1; }
                if (!js_expect(':', &p, end)) { free(k2); free(key); free(ts); if (err) snprintf(err, 256, "safetensors: expected ':' in tensor obj"); return -1; }

                if (strcmp(k2, "dtype") == 0) {
                    free(dtype_s);
                    dtype_s = NULL;
                    if (!js_parse_string(&p, end, &dtype_s)) { free(k2); free(key); free(ts); if (err) snprintf(err, 256, "safetensors: dtype not string"); return -1; }
                } else if (strcmp(k2, "shape") == 0) {
                    if (!st_parse_shape_u32(&p, end, dims, &rank)) { free(k2); free(key); free(ts); if (err) snprintf(err, 256, "safetensors: bad shape"); return -1; }
                } else if (strcmp(k2, "data_offsets") == 0) {
                    if (!st_parse_offsets(&p, end, &off_a, &off_b)) { free(k2); free(key); free(ts); if (err) snprintf(err, 256, "safetensors: bad data_offsets"); return -1; }
                } else {
                    if (!js_skip_value(&p, end)) { free(k2); free(key); free(ts); if (err) snprintf(err, 256, "safetensors: bad unknown value"); return -1; }
                }

                free(k2);
                p = js_skip_ws(p, end);
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; break; }
                // loop continues
            }

            uint32_t dtype = 0;
            if (!dtype_s || !st_dtype_from_str(dtype_s, &dtype) || rank == 0 || off_b <= off_a) {
                free(dtype_s);
                free(key);
                free(ts);
                if (err) snprintf(err, 256, "safetensors: incomplete tensor entry for key");
                return -1;
            }

            free(dtype_s);

            if (n == cap) {
                cap *= 2;
                HfStTensor *ts2 = (HfStTensor*)realloc(ts, (size_t)cap * sizeof(HfStTensor));
                if (!ts2) { free(key); free(ts); if (err) snprintf(err, 256, "safetensors: OOM grow table"); return -1; }
                // zero new tail
                memset(ts2 + n, 0, (size_t)(cap - n) * sizeof(HfStTensor));
                ts = ts2;
            }

            ts[n].name = key; // takes ownership
            ts[n].dtype = dtype;
            ts[n].rank = rank;
            memcpy(ts[n].dims, dims, sizeof(dims));
            ts[n].data_start = off_a;
            ts[n].data_end   = off_b;
            n++;
        }

        p = js_skip_ws(p, end);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
    }

    *out_tensors = ts;
    *out_count = n;
    return 0;
}

static int st_open_file(HfStFile *f, const char *path, char err[256]) {
    if (err) err[0] = 0;
    st_file_init(f);

    snprintf(f->path, sizeof(f->path), "%s", path);

    f->fd = open(path, O_RDONLY);
    if (f->fd < 0) {
        if (err) snprintf(err, 256, "safetensors: open failed: %s", strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(f->fd, &st) != 0) {
        if (err) snprintf(err, 256, "safetensors: fstat failed: %s", strerror(errno));
        st_file_destroy(f);
        return -1;
    }
    f->size = (uint64_t)st.st_size;

    f->base = (uint8_t*)mmap(NULL, (size_t)f->size, PROT_READ, MAP_PRIVATE, f->fd, 0);
    if (f->base == MAP_FAILED) {
        f->base = NULL;
        if (err) snprintf(err, 256, "safetensors: mmap failed: %s", strerror(errno));
        st_file_destroy(f);
        return -1;
    }

    if (f->size < 8) {
        if (err) snprintf(err, 256, "safetensors: file too small");
        st_file_destroy(f);
        return -1;
    }

    uint64_t header_len = rd_u64_le(f->base);
    uint64_t header_off = 8;
    if (header_off + header_len > f->size) {
        if (err) snprintf(err, 256, "safetensors: bad header length");
        st_file_destroy(f);
        return -1;
    }

    const char *json = (const char*)(f->base + header_off);
    if (st_parse_header_json(json, header_len, &f->tensors, &f->ntensors, err) != 0) {
        st_file_destroy(f);
        return -1;
    }

    // Convert data_offsets from "data section offsets" to absolute file offsets:
    // safetensors data offsets are relative to start of data section (right after header).
    // Data section begins at: 8 + header_len
    uint64_t data_base = header_off + header_len;
    for (uint32_t i = 0; i < f->ntensors; i++) {
        uint64_t a = f->tensors[i].data_start;
        uint64_t b = f->tensors[i].data_end;

        // shift into file coordinates
        f->tensors[i].data_start = data_base + a;
        f->tensors[i].data_end   = data_base + b;

        if (f->tensors[i].data_end > f->size || f->tensors[i].data_start > f->tensors[i].data_end) {
            if (err) snprintf(err, 256, "safetensors: tensor '%s' offsets out of range", f->tensors[i].name);
            st_file_destroy(f);
            return -1;
        }
    }

    return 0;
}

static const HfStTensor* st_find_tensor(const HfStFile *f, const char *name) {
    // v1: linear search. (later: hash table)
    for (uint32_t i = 0; i < f->ntensors; i++) {
        if (strcmp(f->tensors[i].name, name) == 0) return &f->tensors[i];
    }
    return NULL;
}




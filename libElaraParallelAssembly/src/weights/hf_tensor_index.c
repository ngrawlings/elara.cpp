// src/weights/hf_tensor_index.c
#include "hf_tensor_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct HfTensorIndexEntry {
    char *name;   // tensor name
    char *file;   // shard filename (relative)
};

struct HfTensorIndex {
    struct HfTensorIndexEntry *e;
    uint32_t n;
    uint32_t cap;
};

static int read_entire_file(const char *path, char **out_buf, size_t *out_sz, char err[256]) {
    if (err) err[0] = 0;
    *out_buf = NULL;
    if (out_sz) *out_sz = 0;

    FILE *f = fopen(path, "rb");
    if (!f) { if (err) snprintf(err, 256, "hf_index_load_json: could not open %s", path); return -1; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); if (err) snprintf(err, 256, "hf_index_load_json: fseek end failed"); return -1; }
    long n = ftell(f);
    if (n < 0) { fclose(f); if (err) snprintf(err, 256, "hf_index_load_json: ftell failed"); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); if (err) snprintf(err, 256, "hf_index_load_json: fseek set failed"); return -1; }

    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); if (err) snprintf(err, 256, "hf_index_load_json: OOM (%ld bytes)", n); return -1; }

    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) { free(buf); if (err) snprintf(err, 256, "hf_index_load_json: short read"); return -1; }

    buf[n] = 0;
    *out_buf = buf;
    if (out_sz) *out_sz = (size_t)n;
    return 0;
}

static const char* skip_ws(const char *p) {
    while (*p && (unsigned char)*p <= 32) p++;
    return p;
}

static int expect_ch(char ch, const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p != ch) return 0;
    *pp = p + 1;
    return 1;
}

static int parse_json_string(const char **pp, char **out_s) {
    const char *p = skip_ws(*pp);
    if (*p != '"') return 0;
    p++;

    // allocate worst-case
    size_t maxlen = strlen(p) + 1;
    char *buf = (char*)malloc(maxlen);
    if (!buf) return 0;

    size_t w = 0;
    while (*p) {
        char c = *p++;
        if (c == '"') break;
        if (c == '\\') {
            char e = *p++;
            if (!e) { free(buf); return 0; }
            // minimal escape support
            if (e == '"' || e == '\\' || e == '/') c = e;
            else if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else c = e;
        }
        buf[w++] = c;
    }
    buf[w] = 0;

    *out_s = buf;
    *pp = p;
    return 1;
}

static int index_grow(struct HfTensorIndex *ix, char err[256]) {
    if (ix->n < ix->cap) return 0;
    uint32_t new_cap = ix->cap ? ix->cap * 2 : 256;
    struct HfTensorIndexEntry *ne =
        (struct HfTensorIndexEntry*)realloc(ix->e, (size_t)new_cap * sizeof(*ne));
    if (!ne) {
        if (err) snprintf(err, 256, "hf_index_load_json: OOM growing index table");
        return -1;
    }
    ix->e = ne;
    ix->cap = new_cap;
    return 0;
}

static const char *find_weight_map_object(const char *buf) {
    // Find `"weight_map"` key
    const char *p = buf;
    const char *needle = "weight_map";
    const size_t nlen = 10; // strlen("weight_map")

    while ((p = strstr(p, needle)) != NULL) {
        // ensure it's inside quotes: ... "weight_map" ...
        // strstr points at the 'w'
        if (p > buf && p[-1] == '"' && p[nlen] == '"') {
            // move to ':' after it
            const char *q = strchr(p + nlen + 1, ':'); // after closing quote
            if (!q) return NULL;
            q++;
            q = skip_ws(q);
            if (*q == '{') return q; // points at '{'
            return NULL;
        }
        p += nlen;
    }
    return NULL;
}

int hf_index_load_json(HfTensorIndex **out, const char *index_json_path, char err[256]) {
    if (err) err[0] = 0;
    if (!out || !index_json_path || !index_json_path[0]) {
        if (err) snprintf(err, 256, "hf_index_load_json: invalid args");
        return -1;
    }
    *out = NULL;

    char *buf = NULL;
    if (read_entire_file(index_json_path, &buf, NULL, err) != 0) return -1;

    const char *p = find_weight_map_object(buf);
    if (!p) {
        free(buf);
        if (err) snprintf(err, 256, "hf_index_load_json: missing \"weight_map\" object");
        return -1;
    }

    struct HfTensorIndex *ix = (struct HfTensorIndex*)calloc(1, sizeof(*ix));
    if (!ix) {
        free(buf);
        if (err) snprintf(err, 256, "hf_index_load_json: OOM");
        return -1;
    }

    // p points at '{'
    if (!expect_ch('{', &p)) {
        free(buf);
        hf_index_destroy(ix);
        if (err) snprintf(err, 256, "hf_index_load_json: expected '{' after weight_map");
        return -1;
    }

    for (;;) {
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == 0) {
            free(buf);
            hf_index_destroy(ix);
            if (err) snprintf(err, 256, "hf_index_load_json: unexpected EOF in weight_map");
            return -1;
        }

        char *name = NULL;
        char *file = NULL;

        if (!parse_json_string(&p, &name)) {
            free(buf);
            hf_index_destroy(ix);
            if (err) snprintf(err, 256, "hf_index_load_json: bad tensor key string");
            return -1;
        }

        if (!expect_ch(':', &p)) {
            free(name);
            free(buf);
            hf_index_destroy(ix);
            if (err) snprintf(err, 256, "hf_index_load_json: expected ':' after tensor key");
            return -1;
        }

        if (!parse_json_string(&p, &file)) {
            free(name);
            free(buf);
            hf_index_destroy(ix);
            if (err) snprintf(err, 256, "hf_index_load_json: bad shard filename string");
            return -1;
        }

        if (index_grow(ix, err) != 0) {
            free(name);
            free(file);
            free(buf);
            hf_index_destroy(ix);
            return -1;
        }

        ix->e[ix->n].name = name;
        ix->e[ix->n].file = file;
        ix->n++;

        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }

        // tolerate trailing whitespace but nothing else
        p = skip_ws(p);
        if (*p == '}') { p++; break; }

        free(buf);
        hf_index_destroy(ix);
        if (err) snprintf(err, 256, "hf_index_load_json: malformed weight_map (expected ',' or '}')");
        return -1;
    }

    free(buf);

    if (ix->n == 0) {
        hf_index_destroy(ix);
        if (err) snprintf(err, 256, "hf_index_load_json: weight_map is empty");
        return -1;
    }

    *out = ix;
    return 0;
}

int hf_index_find_safetensors_shard(const HfTensorIndex *ix,
                                    const char *tensor_name,
                                    char *out_relpath,
                                    size_t out_sz,
                                    char err[256])
{
    if (err) err[0] = 0;
    if (!ix || !tensor_name || !tensor_name[0] || !out_relpath || out_sz == 0) {
        if (err) snprintf(err, 256, "hf_index_find_safetensors_shard: invalid args");
        return -1;
    }

    // v1: linear search (fine)
    for (uint32_t i = 0; i < ix->n; i++) {
        if (strcmp(ix->e[i].name, tensor_name) == 0) {
            snprintf(out_relpath, out_sz, "%s", ix->e[i].file);
            return 0;
        }
    }

    if (err) snprintf(err, 256, "hf_index_find_safetensors_shard: tensor not found in weight_map: %s", tensor_name);
    return -1;
}

int hf_index_list_names(const HfTensorIndex *ix, hf_list_cb cb, void *user, char err[256]) {
    if (err) err[0] = 0;
    if (!ix || !cb) {
        if (err) snprintf(err, 256, "hf_index_list_names: invalid args");
        return -1;
    }

    for (uint32_t i = 0; i < ix->n; i++) {
        cb(user, ix->e[i].name);
    }
    return 0;
}

void hf_index_destroy(HfTensorIndex *ix) {
    if (!ix) return;
    for (uint32_t i = 0; i < ix->n; i++) {
        free(ix->e[i].name);
        free(ix->e[i].file);
    }
    free(ix->e);
    free(ix);
}

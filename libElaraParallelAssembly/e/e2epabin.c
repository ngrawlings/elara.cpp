#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_emit_epa.h"
#include "e_preprocess.h"

#include "../src/epa_asm_compiler.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EPA_BUNDLE_MAGIC "EPABNDL1"
#define EPA_BUNDLE_VERSION 1u
#define EPA_BUNDLE_FLAG_ROOT    0x00000001u
#define EPA_BUNDLE_FLAG_STARTED 0x00000002u

typedef struct {
  char *input_path;
  char *path_id;
  char *epaasm_path;
  char *blob_path;
  uint8_t *blob;
  size_t blob_len;
  uint32_t flags;
} BundleEntry;

typedef struct {
  uint32_t path_id_offset;
  uint32_t path_id_len;
  uint32_t blob_offset;
  uint32_t blob_len;
  uint32_t flags;
  uint32_t reserved;
} BundleIndexEntry;

static char *dup_cstr(const char *src) {
  size_t n;
  char *copy;

  if (!src) return NULL;
  n = strlen(src);
  copy = (char*)malloc(n + 1u);
  if (!copy) return NULL;
  memcpy(copy, src, n + 1u);
  return copy;
}

static char *join2(const char *left, const char *right) {
  size_t a = strlen(left);
  size_t b = strlen(right);
  int need_slash = (a > 0u && left[a - 1u] != '/');
  char *out = (char*)malloc(a + (size_t)need_slash + b + 1u);
  if (!out) return NULL;
  memcpy(out, left, a);
  if (need_slash) out[a++] = '/';
  memcpy(out + a, right, b);
  out[a + b] = 0;
  return out;
}

static char *dir_of_path_dup(const char *path) {
  char *copy;
  char *slash;

  if (!path) return NULL;
  copy = dup_cstr(path);
  if (!copy) return NULL;
  slash = strrchr(copy, '/');
  if (!slash) {
    free(copy);
    return dup_cstr(".");
  }
  if (slash == copy) {
    slash[1] = 0;
    return copy;
  }
  *slash = 0;
  return copy;
}

static char *default_build_dir_from_argv0(const char *argv0) {
  char resolved[PATH_MAX];
  char cwd[PATH_MAX];
  char *last_slash;

  if (!argv0) return dup_cstr("../build");
  if (argv0[0] == '/') {
    snprintf(resolved, sizeof(resolved), "%s", argv0);
  } else if (strchr(argv0, '/')) {
    if (!getcwd(cwd, sizeof(cwd))) {
      return dup_cstr("../build");
    }
    snprintf(resolved, sizeof(resolved), "%s/%s", cwd, argv0);
  } else {
    return dup_cstr("../build");
  }

  last_slash = strrchr(resolved, '/');
  if (!last_slash) return dup_cstr("../build");
  *last_slash = 0;
  last_slash = strrchr(resolved, '/');
  if (!last_slash) return dup_cstr("../build");
  *last_slash = 0;
  return dup_cstr(resolved);
}

static int ensure_dir(const char *path, char err[256]) {
  struct stat st;
  char tmp[PATH_MAX];
  size_t i;
  size_t n;

  if (!path || !path[0]) return 0;
  n = strlen(path);
  if (n >= sizeof(tmp)) {
    if (err) snprintf(err, 256, "path too long: %s", path);
    return 0;
  }

  memcpy(tmp, path, n + 1u);
  for (i = 1; i < n; i++) {
    if (tmp[i] != '/') continue;
    tmp[i] = 0;
    if (tmp[0] && stat(tmp, &st) != 0) {
      if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
        if (err) snprintf(err, 256, "mkdir failed: %s", tmp);
        return 0;
      }
    }
    tmp[i] = '/';
  }

  if (stat(tmp, &st) != 0) {
    if (mkdir(tmp, 0775) != 0 && errno != EEXIST) {
      if (err) snprintf(err, 256, "mkdir failed: %s", tmp);
      return 0;
    }
  }
  return 1;
}

static int ensure_parent_dir(const char *path, char err[256]) {
  char tmp[PATH_MAX];
  char *slash;
  size_t n;

  if (!path || !path[0]) return 0;
  n = strlen(path);
  if (n >= sizeof(tmp)) {
    if (err) snprintf(err, 256, "path too long: %s", path);
    return 0;
  }
  memcpy(tmp, path, n + 1u);
  slash = strrchr(tmp, '/');
  if (!slash) return 1;
  *slash = 0;
  return ensure_dir(tmp, err);
}

static int write_file_bin(const char *path, const void *data, size_t len, char err[256]) {
  FILE *f;
  if (!ensure_parent_dir(path, err)) return 0;
  f = fopen(path, "wb");
  if (!f) {
    if (err) snprintf(err, 256, "open failed: %s", path);
    return 0;
  }
  if (len > 0u && fwrite(data, 1, len, f) != len) {
    fclose(f);
    if (err) snprintf(err, 256, "write failed: %s", path);
    return 0;
  }
  fclose(f);
  return 1;
}

static void write_u32_le(FILE *out, uint32_t v) {
  uint8_t b[4];
  b[0] = (uint8_t)(v & 0xFFu);
  b[1] = (uint8_t)((v >> 8) & 0xFFu);
  b[2] = (uint8_t)((v >> 16) & 0xFFu);
  b[3] = (uint8_t)((v >> 24) & 0xFFu);
  fwrite(b, 1, 4u, out);
}

static const char *basename_no_ext(const char *path) {
  const char *base = strrchr(path, '/');
  return base ? base + 1 : path;
}

static int has_suffix(const char *path, const char *suffix) {
  size_t path_len;
  size_t suffix_len;

  if (!path || !suffix) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) return 0;
  return strcmp(path + (path_len - suffix_len), suffix) == 0;
}

static int is_entry_e_path(const char *path) {
  const char *base = basename_no_ext(path);
  return strcmp(base, "entry.e") == 0;
}

static char *path_id_from_input(const char *path) {
  const char *base = basename_no_ext(path);
  const char *dot = strrchr(base, '.');
  size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
  char *out = (char*)malloc(len + 2u);
  size_t i;
  size_t j = 0;
  int last_dot = 0;

  if (!out) return NULL;

  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)base[i];
    if (isalnum(ch) || ch == '_') {
      out[j++] = (char)ch;
      last_dot = 0;
    } else if (!last_dot && j > 0u) {
      out[j++] = '.';
      last_dot = 1;
    }
  }

  while (j > 0u && out[j - 1u] == '.') j--;
  if (j == 0u) {
    out[j++] = 'k';
  }
  out[j] = 0;
  return out;
}

static int path_id_exists(BundleEntry *entries, int count, const char *path_id) {
  int i;
  for (i = 0; i < count; i++) {
    if (entries[i].path_id && strcmp(entries[i].path_id, path_id) == 0) return 1;
  }
  return 0;
}

static int emit_e_to_epaasm(const char *input_path, const char *epaasm_path, char err[256]) {
  char *src;
  ETokenVec toks;
  EProgram prog;
  ESemanticModel model;
  FILE *out;

  src = e_load_translation_unit(input_path, err);
  if (!src) return 0;

  if (!e_lex_source(src, &toks, err)) {
    snprintf(err, 256, "lex: %s", err[0] ? err : "unknown error");
    free(src);
    return 0;
  }

  if (!e_parse_program(&toks, &prog, err)) {
    char local[256];
    snprintf(local, sizeof(local), "parse: %s", err[0] ? err : "unknown error");
    strncpy(err, local, 255u);
    err[255] = 0;
    e_token_vec_free(&toks);
    free(src);
    return 0;
  }

  if (!e_build_semantic_model(&prog, &model, err)) {
    char local[256];
    snprintf(local, sizeof(local), "semantic: %s", err[0] ? err : "unknown error");
    strncpy(err, local, 255u);
    err[255] = 0;
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 0;
  }

  if (!ensure_parent_dir(epaasm_path, err)) {
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 0;
  }

  out = fopen(epaasm_path, "wb");
  if (!out) {
    snprintf(err, 256, "open failed: %s", epaasm_path);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 0;
  }

  if (!e_emit_epa_asm(out, NULL, &prog, &model, NULL, NULL, err)) {
    char local[256];
    snprintf(local, sizeof(local), "emit: %s", err[0] ? err : "unknown error");
    strncpy(err, local, 255u);
    err[255] = 0;
    fclose(out);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 0;
  }

  fclose(out);
  e_semantic_model_free(&model);
  e_program_free(&prog);
  e_token_vec_free(&toks);
  free(src);
  return 1;
}

static void free_entries(BundleEntry *entries, int count) {
  int i;
  if (!entries) return;
  for (i = 0; i < count; i++) {
    free(entries[i].input_path);
    free(entries[i].path_id);
    free(entries[i].epaasm_path);
    free(entries[i].blob_path);
    free(entries[i].blob);
  }
  free(entries);
}

static int compile_entry(BundleEntry *entry, char err[256]) {
  if (!emit_e_to_epaasm(entry->input_path, entry->epaasm_path, err)) return 0;
  entry->blob = epa_asm_compile_file(entry->epaasm_path, &entry->blob_len, err);
  if (!entry->blob) {
    snprintf(err, 256, "asm: %s", err[0] ? err : "unknown error");
    return 0;
  }
  if (!write_file_bin(entry->blob_path, entry->blob, entry->blob_len, err)) return 0;
  return 1;
}

static int write_bundle(const char *out_path, BundleEntry *entries, int entry_count, char err[256]) {
  FILE *out;
  BundleIndexEntry *index;
  uint32_t header_size = 8u + 4u + 4u;
  uint32_t index_size = (uint32_t)entry_count * (uint32_t)sizeof(BundleIndexEntry);
  uint32_t cursor = header_size + index_size;
  int i;

  if (!ensure_parent_dir(out_path, err)) return 0;
  out = fopen(out_path, "wb");
  if (!out) {
    snprintf(err, 256, "open failed: %s", out_path);
    return 0;
  }

  index = (BundleIndexEntry*)calloc((size_t)entry_count, sizeof(BundleIndexEntry));
  if (!index) {
    fclose(out);
    snprintf(err, 256, "OOM");
    return 0;
  }

  for (i = 0; i < entry_count; i++) {
    size_t path_len = strlen(entries[i].path_id);
    index[i].path_id_offset = cursor;
    index[i].path_id_len = (uint32_t)path_len;
    cursor += (uint32_t)path_len;
  }

  for (i = 0; i < entry_count; i++) {
    index[i].blob_offset = cursor;
    index[i].blob_len = (uint32_t)entries[i].blob_len;
    index[i].flags = entries[i].flags;
    index[i].reserved = 0u;
    cursor += (uint32_t)entries[i].blob_len;
  }

  fwrite(EPA_BUNDLE_MAGIC, 1, 8u, out);
  write_u32_le(out, EPA_BUNDLE_VERSION);
  write_u32_le(out, (uint32_t)entry_count);

  for (i = 0; i < entry_count; i++) {
    write_u32_le(out, index[i].path_id_offset);
    write_u32_le(out, index[i].path_id_len);
    write_u32_le(out, index[i].blob_offset);
    write_u32_le(out, index[i].blob_len);
    write_u32_le(out, index[i].flags);
    write_u32_le(out, index[i].reserved);
  }

  for (i = 0; i < entry_count; i++) {
    fwrite(entries[i].path_id, 1, strlen(entries[i].path_id), out);
  }

  for (i = 0; i < entry_count; i++) {
    if (entries[i].blob_len > 0u) {
      fwrite(entries[i].blob, 1, entries[i].blob_len, out);
    }
  }

  free(index);
  fclose(out);
  return 1;
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [--out ../build/epa.bin] <root.e> <child1.e> [child2.e ...]\n", argv0);
}

int main(int argc, char **argv) {
  const char *out_path = NULL;
  int argi = 1;
  int input_count;
  BundleEntry *entries;
  char err[256];
  char *build_dir = NULL;
  char *default_out_path = NULL;
  char *out_dir = NULL;
  char *epaasm_dir = NULL;
  char *blob_dir = NULL;
  int i;
  int entry_index = -1;

  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }

  while (argi < argc && argv[argi][0] == '-') {
    if (strcmp(argv[argi], "--out") == 0) {
      if (argi + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      out_path = argv[argi + 1];
      argi += 2;
      continue;
    }
    usage(argv[0]);
    return 2;
  }

  input_count = argc - argi;
  if (input_count <= 0) {
    usage(argv[0]);
    return 2;
  }

  for (i = 0; i < input_count; i++) {
    if (has_suffix(argv[argi + i], ".em")) {
      fprintf(stderr, ".em files are include-only and cannot be compiled directly: %s\n", argv[argi + i]);
      return 1;
    }
    if (!has_suffix(argv[argi + i], ".e")) {
      fprintf(stderr, "expected a .e compile unit: %s\n", argv[argi + i]);
      return 1;
    }
    if (is_entry_e_path(argv[argi + i])) {
      if (entry_index >= 0) {
        fprintf(stderr, "exactly one entry.e root compile unit is required\n");
        return 1;
      }
      entry_index = i;
    }
  }
  if (entry_index < 0) {
    fprintf(stderr, "missing root compile unit entry.e\n");
    return 1;
  }

  build_dir = default_build_dir_from_argv0(argv[0]);
  if (!build_dir) {
    fprintf(stderr, "OOM\n");
    return 1;
  }
  default_out_path = join2(build_dir, "epa.bin");
  if (!default_out_path) {
    fprintf(stderr, "OOM\n");
    free(build_dir);
    free(default_out_path);
    return 1;
  }
  if (!out_path) out_path = default_out_path;
  out_dir = dir_of_path_dup(out_path);
  epaasm_dir = join2(out_dir ? out_dir : build_dir, "epaasm");
  blob_dir = join2(out_dir ? out_dir : build_dir, "blobs");
  if (!out_dir || !epaasm_dir || !blob_dir) {
    fprintf(stderr, "OOM\n");
    free(build_dir);
    free(default_out_path);
    free(out_dir);
    free(epaasm_dir);
    free(blob_dir);
    return 1;
  }

  entries = (BundleEntry*)calloc((size_t)input_count, sizeof(BundleEntry));
  if (!entries) {
    fprintf(stderr, "OOM\n");
    free(build_dir);
    free(default_out_path);
    free(out_dir);
    free(epaasm_dir);
    free(blob_dir);
    return 1;
  }

  for (i = 0; i < input_count; i++) {
    const char *input_path = argv[argi + ((i == 0) ? entry_index : (i <= entry_index ? i - 1 : i))];
    const char *base = basename_no_ext(input_path);
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    char stem[PATH_MAX];

    if (stem_len >= sizeof(stem)) {
      fprintf(stderr, "path too long: %s\n", input_path);
      free_entries(entries, input_count);
      free(build_dir);
      free(default_out_path);
      free(out_dir);
      free(epaasm_dir);
      free(blob_dir);
      return 1;
    }

    memcpy(stem, base, stem_len);
    stem[stem_len] = 0;

    entries[i].input_path = dup_cstr(input_path);
    entries[i].path_id = path_id_from_input(input_path);
    entries[i].flags = EPA_BUNDLE_FLAG_STARTED;
    if (i == 0) entries[i].flags |= EPA_BUNDLE_FLAG_ROOT;

    if (!entries[i].input_path || !entries[i].path_id) {
      fprintf(stderr, "OOM\n");
      free_entries(entries, input_count);
      free(build_dir);
      free(default_out_path);
      free(out_dir);
      free(epaasm_dir);
      free(blob_dir);
      return 1;
    }

    if (path_id_exists(entries, i, entries[i].path_id)) {
      fprintf(stderr, "duplicate path id: %s\n", entries[i].path_id);
      free_entries(entries, input_count);
      free(build_dir);
      free(default_out_path);
      free(out_dir);
      free(epaasm_dir);
      free(blob_dir);
      return 1;
    }

    {
      size_t epaasm_len = strlen(epaasm_dir) + 1u + strlen(stem) + strlen(".epaasm") + 1u;
      size_t blob_len = strlen(blob_dir) + 1u + strlen(stem) + strlen(".epa.bin") + 1u;
      entries[i].epaasm_path = (char*)malloc(epaasm_len);
      entries[i].blob_path = (char*)malloc(blob_len);
      if (entries[i].epaasm_path) {
        snprintf(entries[i].epaasm_path, epaasm_len, "%s/%s.epaasm", epaasm_dir, stem);
      }
      if (entries[i].blob_path) {
        snprintf(entries[i].blob_path, blob_len, "%s/%s.epa.bin", blob_dir, stem);
      }
    }

    if (!entries[i].epaasm_path || !entries[i].blob_path) {
      fprintf(stderr, "OOM\n");
      free_entries(entries, input_count);
      free(build_dir);
      free(default_out_path);
      free(out_dir);
      free(epaasm_dir);
      free(blob_dir);
      return 1;
    }

    if (!compile_entry(&entries[i], err)) {
      fprintf(stderr, "%s: %s\n", input_path, err[0] ? err : "compile failed");
      free_entries(entries, input_count);
      free(build_dir);
      free(default_out_path);
      free(epaasm_dir);
      free(blob_dir);
      return 1;
    }
  }

  if (!write_bundle(out_path, entries, input_count, err)) {
    fprintf(stderr, "%s\n", err[0] ? err : "bundle write failed");
    free_entries(entries, input_count);
    free(build_dir);
    free(default_out_path);
    free(out_dir);
    free(epaasm_dir);
    free(blob_dir);
    return 1;
  }

  free_entries(entries, input_count);
  free(build_dir);
  free(default_out_path);
  free(out_dir);
  free(epaasm_dir);
  free(blob_dir);
  return 0;
}

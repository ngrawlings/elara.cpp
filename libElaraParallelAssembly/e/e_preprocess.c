#define _POSIX_C_SOURCE 200809L
#include "e_preprocess.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int has_suffix(const char *path, const char *suffix) {
  size_t path_len;
  size_t suffix_len;

  if (!path || !suffix) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) return 0;
  return strcmp(path + (path_len - suffix_len), suffix) == 0;
}

static char *slurp_fd(int fd, char err[256]) {
  char tmp[4096];
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;

  for (;;) {
    ssize_t got = read(fd, tmp, sizeof(tmp));
    if (got < 0) {
      free(buf);
      if (err) snprintf(err, 256, "read failed: %s", strerror(errno));
      return NULL;
    }
    if (got == 0) break;
    if (len + (size_t)got + 1u > cap) {
      size_t next = cap ? cap * 2u : 8192u;
      while (next < len + (size_t)got + 1u) next *= 2u;
      {
        char *nbuf = (char*)realloc(buf, next);
        if (!nbuf) {
          free(buf);
          if (err) snprintf(err, 256, "OOM");
          return NULL;
        }
        buf = nbuf;
        cap = next;
      }
    }
    memcpy(buf + len, tmp, (size_t)got);
    len += (size_t)got;
  }

  if (!buf) {
    buf = (char*)malloc(1u);
    if (!buf) {
      if (err) snprintf(err, 256, "OOM");
      return NULL;
    }
  }
  buf[len] = 0;
  return buf;
}

char *e_load_translation_unit(const char *path, char err[256]) {
  int fds[2];
  pid_t pid;
  char *out;
  int status;

  if (err) err[0] = 0;
  if (!path || !path[0]) {
    if (err) snprintf(err, 256, "missing input path");
    return NULL;
  }

  if (has_suffix(path, ".em")) {
    if (err) snprintf(err, 256, ".em files are include-only and cannot be compiled directly: %s", path);
    return NULL;
  }
  if (!has_suffix(path, ".e")) {
    if (err) snprintf(err, 256, "expected a .e compile unit: %s", path);
    return NULL;
  }

  if (pipe(fds) != 0) {
    if (err) snprintf(err, 256, "pipe failed: %s", strerror(errno));
    return NULL;
  }

  pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    if (err) snprintf(err, 256, "fork failed: %s", strerror(errno));
    return NULL;
  }

  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    execlp("cc", "cc", "-E", "-P", "-undef", "-x", "c", path, (char*)NULL);
    _exit(127);
  }

  close(fds[1]);
  out = slurp_fd(fds[0], err);
  close(fds[0]);
  if (!out) {
    waitpid(pid, &status, 0);
    return NULL;
  }

  if (waitpid(pid, &status, 0) < 0) {
    free(out);
    if (err) snprintf(err, 256, "waitpid failed: %s", strerror(errno));
    return NULL;
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (err && out[0]) {
      strncpy(err, out, 255u);
      err[255] = 0;
    } else if (err) {
      snprintf(err, 256, "preprocess failed: %s", path);
    }
    free(out);
    return NULL;
  }

  return out;
}

/* ------------------------------------------------------------------ */
/* ELineMap helpers                                                     */
/* ------------------------------------------------------------------ */

void e_line_map_free(ELineMap *map) {
  size_t i;
  if (!map) return;
  for (i = 0; i < map->filename_count; i++) free(map->filenames[i]);
  free(map->filenames);
  free(map->file_indices);
  free(map->line_nums);
  memset(map, 0, sizeof(*map));
}

static unsigned int linemap_intern(ELineMap *map, const char *fname) {
  size_t i;
  char *copy;
  char **nf;

  for (i = 0; i < map->filename_count; i++) {
    if (strcmp(map->filenames[i], fname) == 0) return (unsigned int)i;
  }
  copy = (char*)malloc(strlen(fname) + 1u);
  if (!copy) return 0u;
  strcpy(copy, fname);
  nf = (char**)realloc(map->filenames, sizeof(char*) * (map->filename_count + 1u));
  if (!nf) { free(copy); return 0u; }
  nf[map->filename_count] = copy;
  map->filenames = nf;
  return (unsigned int)(map->filename_count++);
}

static int linemap_push(ELineMap *map, unsigned int fidx, int lineno) {
  size_t n = map->count + 1u;
  unsigned int *nfi = (unsigned int*)realloc(map->file_indices, sizeof(unsigned int) * n);
  int          *nln = (int*)         realloc(map->line_nums,    sizeof(int)          * n);
  if (!nfi || !nln) { free(nfi); free(nln); return 0; }
  map->file_indices = nfi;
  map->line_nums    = nln;
  map->file_indices[map->count] = fidx;
  map->line_nums[map->count]    = lineno;
  map->count++;
  return 1;
}

static int buf_append(char **buf, size_t *len, size_t *cap, const char *data, size_t dlen) {
  if (*len + dlen + 1u > *cap) {
    size_t next = *cap ? *cap * 2u : 8192u;
    char *nb;
    while (next < *len + dlen + 1u) next *= 2u;
    nb = (char*)realloc(*buf, next);
    if (!nb) return 0;
    *buf = nb;
    *cap = next;
  }
  memcpy(*buf + *len, data, dlen);
  *len += dlen;
  (*buf)[*len] = '\0';
  return 1;
}

/* Parse a GCC linemarker "# N "filename" [flags]" from [p, p+len).
   Returns 1 on match, fills *out_lineno and out_fname. */
static int parse_linemarker(const char *p, size_t len,
                             int *out_lineno, char *out_fname, size_t fname_buf) {
  const char *end = p + len;
  int n = 0;
  const char *fs, *fe;
  size_t flen;

  if (p >= end || *p != '#') return 0;
  p++;
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  if (p >= end || !isdigit((unsigned char)*p)) return 0;
  while (p < end && isdigit((unsigned char)*p)) { n = n * 10 + (*p - '0'); p++; }
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  if (p >= end || *p != '"') return 0;
  p++;
  fs = p;
  while (p < end && *p != '"' && *p != '\n') p++;
  if (p >= end || *p != '"') return 0;
  fe = p;

  *out_lineno = n;
  flen = (size_t)(fe - fs);
  if (flen >= fname_buf) flen = fname_buf - 1u;
  memcpy(out_fname, fs, flen);
  out_fname[flen] = '\0';
  return 1;
}

char *e_load_translation_unit_with_map(const char *path, ELineMap *line_map, char err[256]) {
  int fds[2];
  pid_t pid;
  char *raw = NULL;
  int status;
  char *cleaned = NULL;
  size_t cleaned_len = 0, cleaned_cap = 0;
  const char *p;
  char fname_buf[512];
  unsigned int cur_fidx = 0;
  int cur_line = 1;
  int lm_lineno;

  if (err) err[0] = 0;
  if (!path || !path[0]) {
    if (err) snprintf(err, 256, "missing input path");
    return NULL;
  }
  if (has_suffix(path, ".em")) {
    if (err) snprintf(err, 256, ".em files are include-only and cannot be compiled directly: %s", path);
    return NULL;
  }
  if (!has_suffix(path, ".e")) {
    if (err) snprintf(err, 256, "expected a .e compile unit: %s", path);
    return NULL;
  }

  if (line_map) {
    memset(line_map, 0, sizeof(*line_map));
    cur_fidx = linemap_intern(line_map, path);
  }

  if (pipe(fds) != 0) {
    if (err) snprintf(err, 256, "pipe failed: %s", strerror(errno));
    return NULL;
  }

  pid = fork();
  if (pid < 0) {
    close(fds[0]); close(fds[1]);
    if (err) snprintf(err, 256, "fork failed: %s", strerror(errno));
    return NULL;
  }

  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]); close(fds[1]);
    /* No -P: keep linemarkers so we can build the source map */
    execlp("cc", "cc", "-E", "-undef", "-x", "c",
           "-I", ".",
           "-I", "libElaraParallelAssembly/e",
           "-I", "./libElaraParallelAssembly/e",
           path, (char*)NULL);
    _exit(127);
  }

  close(fds[1]);
  raw = slurp_fd(fds[0], err);
  close(fds[0]);
  if (!raw) { waitpid(pid, &status, 0); return NULL; }

  if (waitpid(pid, &status, 0) < 0 ||
      !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (err && raw[0]) { strncpy(err, raw, 255u); err[255] = 0; }
    else if (err) snprintf(err, 256, "preprocess failed: %s", path);
    free(raw);
    if (line_map) e_line_map_free(line_map);
    return NULL;
  }

  /* Parse raw output: strip linemarkers, build map */
  p = raw;
  while (*p) {
    const char *line_end = p;
    size_t line_len;

    while (*line_end && *line_end != '\n') line_end++;
    line_len = (size_t)(line_end - p);

    if (parse_linemarker(p, line_len, &lm_lineno, fname_buf, sizeof(fname_buf))) {
      /* Linemarker: update current file/line tracking, skip this line */
      if (line_map) {
        cur_fidx = linemap_intern(line_map, fname_buf);
      }
      cur_line = lm_lineno;
    } else {
      /* Source line: add to cleaned output and map */
      if (!buf_append(&cleaned, &cleaned_len, &cleaned_cap, p, line_len) ||
          !buf_append(&cleaned, &cleaned_len, &cleaned_cap, "\n", 1u)) {
        if (err) snprintf(err, 256, "OOM building cleaned source");
        free(raw); free(cleaned);
        if (line_map) e_line_map_free(line_map);
        return NULL;
      }
      if (line_map) {
        if (!linemap_push(line_map, cur_fidx, cur_line)) {
          if (err) snprintf(err, 256, "OOM building line map");
          free(raw); free(cleaned);
          e_line_map_free(line_map);
          return NULL;
        }
      }
      cur_line++;
    }

    if (*line_end == '\n') line_end++;
    p = line_end;
  }

  free(raw);

  if (!cleaned) {
    cleaned = (char*)malloc(1u);
    if (!cleaned) { if (err) snprintf(err, 256, "OOM"); return NULL; }
    cleaned[0] = '\0';
  }

  return cleaned;
}

#define _POSIX_C_SOURCE 200809L
#include "e_preprocess.h"

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

// epa_server.c
// Standalone EPA backend runner with optional RPC (mongoose) front-end.
// - If launched with --rpc, runs an HTTP server that accepts POST /run (raw EPA blob)
// - Otherwise, runs once locally: reads an input file and writes EPAR0 to output file/stdout.
//
// Build (example):
//   cc -O2 -std=c11 -pthread \
//     epa_server.c epa_backend.c epa_backend_opengl.c epa_backend_cuda.c \
//     epa_result_bundle.c mongoose.c \
//     -o epa_server
//
// Run RPC:
//   ./epa_server --rpc --port 8080 --mode opengl
//
// Run local:
//   ./epa_server --mode opengl --in program.epa --out result.epar0
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>

#include "epa_asm_compiler.h"
#include "gui/viewport.h"
#include "rpc/mongoose.h"

//#ifndef EPA_MAX_ERR
//#define EPA_MAX_ERR 256
//#endif
//
//// ---------------------------
//// Utilities
//// ---------------------------
//static void die(const char *msg) {
//  fprintf(stderr, "FATAL: %s\n", msg);
//  exit(1);
//}
//
//static int streq(const char *a, const char *b) {
//  return a && b && strcmp(a, b) == 0;
//}
//
//// Compare hm->uri to a literal path, ignoring any ?query.
//// Returns 1 if equal, else 0.
//static int uri_eq(const struct mg_http_message *hm, const char *path) {
//  if (!hm || !path) return 0;
//
//  const char *u = hm->uri.buf;
//  size_t ulen = (size_t) hm->uri.len;
//
//  // Trim query string
//  for (size_t i = 0; i < ulen; i++) {
//    if (u[i] == '?') { ulen = i; break; }
//  }
//
//  size_t plen = strlen(path);
//  if (ulen != plen) return 0;
//  return memcmp(u, path, plen) == 0;
//}
//
//static int method_is(const struct mg_http_message *hm, const char *m) {
//  size_t ml = strlen(m);
//  return hm && hm->method.buf && (size_t)hm->method.len == ml &&
//         memcmp(hm->method.buf, m, ml) == 0;
//}
//
//
//static EpaMode parse_mode(const char *s) {
//  if (!s) return EPA_MODE_OPENGL;
//  if (streq(s, "opengl")) return EPA_MODE_OPENGL;
//  if (streq(s, "cuda")) return EPA_MODE_CUDA;
//  // Allow a few aliases
//  if (streq(s, "gl")) return EPA_MODE_OPENGL;
//  if (streq(s, "gpu")) return EPA_MODE_CUDA;
//  return (EpaMode) 0;
//}
//
//static uint8_t *read_file(const char *path, size_t *out_len) {
//  *out_len = 0;
//  FILE *f = fopen(path, "rb");
//  if (!f) return NULL;
//  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
//  long sz = ftell(f);
//  if (sz < 0) { fclose(f); return NULL; }
//  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
//
//  uint8_t *buf = (uint8_t *) malloc((size_t) sz);
//  if (!buf) { fclose(f); return NULL; }
//
//  size_t got = fread(buf, 1, (size_t) sz, f);
//  fclose(f);
//
//  if (got != (size_t) sz) { free(buf); return NULL; }
//  *out_len = got;
//  return buf;
//}
//
//static int write_file(const char *path, const uint8_t *buf, size_t len) {
//  FILE *f = fopen(path, "wb");
//  if (!f) return 0;
//  size_t w = fwrite(buf, 1, len, f);
//  fclose(f);
//  return w == len;
//}
//
//static int ends_with(const char *s, const char *suffix) {
//  if (!s || !suffix) return 0;
//  size_t sl = strlen(s), su = strlen(suffix);
//  if (su > sl) return 0;
//  return memcmp(s + (sl - su), suffix, su) == 0;
//}
//
//static int is_regular_file(const char *path) {
//  struct stat st;
//  if (stat(path, &st) != 0) return 0;
//  return S_ISREG(st.st_mode);
//}
//
//static char *path_join(const char *a, const char *b) {
//  size_t al = strlen(a), bl = strlen(b);
//  int need_slash = (al > 0 && a[al - 1] != '/');
//  size_t n = al + (need_slash ? 1 : 0) + bl + 1;
//  char *p = (char *)malloc(n);
//  if (!p) return NULL;
//  memcpy(p, a, al);
//  size_t k = al;
//  if (need_slash) p[k++] = '/';
//  memcpy(p + k, b, bl);
//  p[k + bl] = 0;
//  return p;
//}
//
//
//// ---------------------------
//// Execution wrapper
//// ---------------------------
//static int epa_execute_once(
//    EpaBackend *backend,
//    const uint8_t *blob, size_t blob_len,
//    uint8_t **out_res, size_t *out_res_len,
//    char err[EPA_MAX_ERR]
//) {
//  if (!backend || !backend->vt || !backend->vt->execute) {
//    strncpy(err, "Backend not initialized", EPA_MAX_ERR - 1);
//    err[EPA_MAX_ERR - 1] = 0;
//    return 0;
//  }
//  err[0] = 0;
//  *out_res = NULL;
//  *out_res_len = 0;
//
//  int ok = backend->vt->execute(backend, blob, blob_len, out_res, out_res_len, err);
//  if (!ok && err[0] == 0) {
//    strncpy(err, "Execution failed (no error message)", EPA_MAX_ERR - 1);
//    err[EPA_MAX_ERR - 1] = 0;
//  }
//  return ok;
//}
//
//static int run_unit_tests(EpaBackend *backend, const char *tests_dir) {
//  DIR *d = opendir(tests_dir);
//  if (!d) {
//    fprintf(stderr, "[UT] ERROR: cannot open dir '%s': %s\n", tests_dir, strerror(errno));
//    return 2;
//  }
//
//  int total = 0;
//  int failed = 0;
//
//  struct dirent *ent;
//  while ((ent = readdir(d)) != NULL) {
//    const char *name = ent->d_name;
//    if (!name || name[0] == '.') continue;
//
//    // only accept .epaasm and .epa
//    int is_asm = ends_with(name, ".epaasm");
//    int is_bin = ends_with(name, ".epa");
//    if (!is_asm && !is_bin) continue;
//
//    char *full = path_join(tests_dir, name);
//    if (!full) {
//      fprintf(stderr, "[UT] ERROR: OOM joining path for '%s'\n", name);
//      failed++;
//      continue;
//    }
//
//    if (!is_regular_file(full)) {
//      free(full);
//      continue;
//    }
//
//    total++;
//
//    // Load program
//    uint8_t *blob = NULL;
//    size_t blob_len = 0;
//    char err[EPA_MAX_ERR];
//    err[0] = 0;
//
//    if (is_asm) {
//      blob = epa_asm_compile_file(full, &blob_len, err);
//      if (!blob) {
//        fprintf(stderr, "[UT] FAIL %s: ASM compile error: %s\n", name, err[0] ? err : "unknown");
//        failed++;
//        free(full);
//        continue;
//      }
//    } else {
//      blob = read_file(full, &blob_len);
//      if (!blob) {
//        fprintf(stderr, "[UT] FAIL %s: cannot read file\n", name);
//        failed++;
//        free(full);
//        continue;
//      }
//    }
//
//    // Execute
//    uint8_t *res = NULL;
//    size_t res_len = 0;
//    err[0] = 0;
//
//    int ok = epa_execute_once(backend, blob, blob_len, &res, &res_len, err);
//
//    free(blob);
//    free(res);
//    free(full);
//
//    if (!ok) {
//      fprintf(stderr, "[UT] FAIL %s: %s\n", name, err[0] ? err : "Execution failed (no error)");
//      failed++;
//      continue;
//    }
//
//    fprintf(stderr, "[UT] PASS %s\n", name);
//  }
//
//  closedir(d);
//
//  fprintf(stderr, "[UT] SUMMARY: total=%d passed=%d failed=%d\n", total, (total - failed), failed);
//
//  return failed ? 1 : 0;
//}
//
//
//// ---------------------------
//// RPC (mongoose)
//// ---------------------------
//typedef struct {
//  struct mg_mgr mgr;
//  struct mg_connection *listener;
//  EpaBackend *backend;
//  int running;
//} RpcServer;
//
//static void http_reply_json(struct mg_connection *c, int code, const char *json) {
//  mg_http_reply(c, code,
//                "Content-Type: application/json\r\n"
//                "Cache-Control: no-store\r\n",
//                "%s", json ? json : "{}");
//}
//
//static void http_reply_error(struct mg_connection *c, int code, const char *msg) {
//  char buf[512];
//  // Minimal JSON escaping for quotes/backslashes/newlines
//  char esc[320];
//  size_t ei = 0;
//  for (const char *p = msg ? msg : ""; *p && ei + 2 < sizeof(esc); p++) {
//    char ch = *p;
//    if (ch == '\\' || ch == '"') { esc[ei++] = '\\'; esc[ei++] = ch; }
//    else if (ch == '\n') { esc[ei++] = '\\'; esc[ei++] = 'n'; }
//    else if (ch == '\r') { esc[ei++] = '\\'; esc[ei++] = 'r'; }
//    else { esc[ei++] = ch; }
//  }
//  esc[ei] = 0;
//
//  snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", esc);
//  http_reply_json(c, code, buf);
//}
//
//static void http_reply_binary(struct mg_connection *c, int code, const uint8_t *bin, size_t len) {
//  mg_http_reply(c, code,
//                "Content-Type: application/octet-stream\r\n"
//                "Cache-Control: no-store\r\n",
//                "");
//  mg_send(c, bin, len);
//}
//
//static void rpc_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
//  RpcServer *srv = (RpcServer *) fn_data;
//
//  if (ev == MG_EV_HTTP_MSG) {
//    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
//
//    // GET /health
//    if (uri_eq(hm, "/health")) {
//      http_reply_json(c, 200, "{\"ok\":true}");
//      return;
//    }
//
//    // POST /run  (body = raw EPA blob; response = EPAR0 binary)
//    if (uri_eq(hm, "/run")) {
//      if (!method_is(hm, "POST")) {
//        http_reply_error(c, 405, "Use POST /run with raw EPA blob in request body");
//        return;
//      }
//
//      const uint8_t *blob = (const uint8_t *) hm->body.buf;
//      size_t blob_len = (size_t) hm->body.len;
//
//      if (blob_len == 0) {
//        http_reply_error(c, 400, "Empty request body");
//        return;
//      }
//
//      uint8_t *res = NULL;
//      size_t res_len = 0;
//      char err[EPA_MAX_ERR];
//
//      int ok = epa_execute_once(srv->backend, blob, blob_len, &res, &res_len, err);
//      if (!ok) {
//        http_reply_error(c, 400, err);
//        if (res) free(res);
//        return;
//      }
//
//      http_reply_binary(c, 200, res, res_len);
//      free(res);
//      return;
//    }
//
//    // Unknown route
//    http_reply_error(c, 404, "Not found. Use /health or POST /run");
//  }
//}
//
//static void rpc_run(EpaBackend *backend, const char *port) {
//  RpcServer srv;
//  memset(&srv, 0, sizeof(srv));
//  srv.backend = backend;
//  srv.running = 1;
//
//  mg_mgr_init(&srv.mgr);
//
//  char listen_addr[64];
//  snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%s", port);
//
//  srv.listener = mg_http_listen(&srv.mgr, listen_addr, rpc_handler, &srv);
//  if (!srv.listener) die("Failed to listen (check port / permissions)");
//
//  fprintf(stderr, "EPA RPC listening on %s\n", listen_addr);
//  fprintf(stderr, "  GET  /health\n");
//  fprintf(stderr, "  POST /run   (body = raw EPA blob; response = EPAR0)\n");
//
//  while (srv.running) {
//    mg_mgr_poll(&srv.mgr, 50);
//
//    // keep the viewport responsive (if you have it globally or via backend->vp)
//    if (srv.backend && srv.backend->vp) {
//      if (!vp_pump(srv.backend->vp)) srv.running = 0;
//    }
//  }
//
//  mg_mgr_free(&srv.mgr);
//}
//
//// ---------------------------
//// main
//// ---------------------------
//static void usage(const char *argv0) {
//  fprintf(stderr,
//    "Usage:\n"
//    "  %s --rpc [--port 8080] [--mode opengl|cuda]\n"
//    "  %s [--mode opengl|cuda] --in file.epa [--out file.epar0]\n"
//    "\n"
//    "Options:\n"
//    "  --rpc            Run HTTP RPC server\n"
//    "  --port <p>       Port for RPC (default 8080)\n"
//    "  --mode <m>       Backend mode: opengl|cuda (default opengl)\n"
//    "  --in <path>      Input EPA blob for local run\n"
//    "  --out <path>     Output EPAR0 file (default: stdout)\n"
//    "  --unittests <dir> Run unit tests found under <dir>\n",
//    argv0, argv0
//  );
//}

//int main(int argc, char **argv) {
//	int kernel_mode = 0;
//  int asmin = 0;
//  int want_rpc = 0;
//  int want_unittests = 0;
//  const char *tests_dir = NULL;
//  const char *port = "8080";
//  const char *mode_s = "opengl";
//  const char *in_path = NULL;
//  const char *out_path = NULL;
//  char err[EPA_MAX_ERR];
//  size_t blob_len = 0;
//  uint8_t *blob = NULL;
//
//  for (int i = 1; i < argc; i++) {
//    if (streq(argv[i], "--rpc")) {
//      want_rpc = 1;
//    } else if (streq(argv[i], "--port") && i + 1 < argc) {
//      port = argv[++i];
//    } else if (streq(argv[i], "--mode") && i + 1 < argc) {
//      mode_s = argv[++i];
//    } else if (streq(argv[i], "--asmin")) {
//      asmin = 1;
//    } else if (streq(argv[i], "--unittests") && i + 1 < argc) {
//      want_unittests = 1;
//      tests_dir = argv[++i];
//    } else if (streq(argv[i], "--in") && i + 1 < argc) {
//      in_path = argv[++i];
//    } else if (streq(argv[i], "--out") && i + 1 < argc) {
//      out_path = argv[++i];
//    } else if (strcmp(argv[i], "--kernel") == 0) {
//          kernel_mode = 1;
//    } else if (streq(argv[i], "--help") || streq(argv[i], "-h")) {
//      usage(argv[0]);
//      return 0;
//    } else {
//      fprintf(stderr, "Unknown arg: %s\n", argv[i]);
//      usage(argv[0]);
//      return 2;
//    }
//  }
//
//  EpaMode mode = parse_mode(mode_s);
//
//  // ---- Viewport (shared by all backends) ----
//  // For now: always create it. Later you can add a flag to disable UI.
//  Viewport *vp = vp_create(800, 600, "EPA Viewport", /*enable_cuda=*/(mode == EPA_MODE_CUDA));
//  if (!vp) {
//    fprintf(stderr, "Failed to create viewport\n");
//    return 2;
//  }
//
//  // Create backend with viewport injected
//  EpaBackend *backend = epa_backend_create(mode, vp);
//  if (!backend) {
//    fprintf(stderr, "Failed to create backend\n");
//    vp_destroy(vp);
//    return 2;
//  }
//
//  if (want_rpc) {
//    rpc_run(backend, port);
//    backend->vt->destroy(backend);
//    vp_destroy(vp);
//    return 0;
//
//  } else if (asmin) {
//  	  blob = epa_asm_compile_file(in_path, &blob_len, err);
//  	  if (!blob) {
//  	    fprintf(stderr, "ASM compile error: %s\n", err);
//  	    backend->vt->destroy(backend);
//  	    vp_destroy(vp);
//  	    return 1;
//  	  }
//
//  } else if (in_path) {
//	  blob = read_file(in_path, &blob_len);
//	  if (!blob) die("Failed to read input file");
//
//  } else if (want_unittests) {
//	  if (!tests_dir) {
//	      fprintf(stderr, "[UT] ERROR: --unittests requires a directory\n");
//	      backend->vt->destroy(backend);
//	      vp_destroy(vp);
//	      return 2;
//	  }
//
//	  int rc = run_unit_tests(backend, tests_dir);
//
//	  backend->vt->destroy(backend);
//	  vp_destroy(vp);
//	  return rc;
//
//  } else {
//	  fprintf(stderr, "Missing --in for local run (or use --rpc)\n");
//	  usage(argv[0]);
//	  backend->vt->destroy(backend);
//	  return 2;
//
//  }
//
//  if (!blob) die("Failed to read input file");
//
//  uint8_t *res = NULL;
//  size_t res_len = 0;
//
//  printf("[UT] blob_len=%zu\n", blob_len);
//  for (size_t i = 0; i < blob_len; i++) {
//    if ((i % 16) == 0)
//      printf("\n%04zx: ", i);
//    printf("%02x ", blob[i]);
//  }
//  printf("\n");
//
//  if (!kernel_mode) {
//	  int ok = epa_execute_once(backend, blob, blob_len, &res, &res_len, err);
//	  if (!ok) {
//		fprintf(stderr, "Execution error: %s\n", err);
//		backend->vt->destroy(backend);
//		free(blob);
//		return 1;
//	  }
//
//	  // Keep window open briefly so you can see the frame (or close it)
//	  for (int i = 0; i < 300; i++) {   // ~300 * 16ms ≈ 5s
//		  if (!vp_pump(vp)) break;
//		  // small sleep without extra deps:
//		  struct timespec ts = {0, 16 * 1000 * 1000};
//		  nanosleep(&ts, NULL);
//	  }
//  } else {
//	  int rc;
//
//	  do {
//	    rc = epa_execute_once(backend, blob, blob_len, &res, &res_len, err);
//	  } while (true);
//  }
//
//  if (out_path) {
//    if (!write_file(out_path, res, res_len)) {
//      free(res);
//      backend->vt->destroy(backend);
//      die("Failed to write output file");
//    }
//    fprintf(stderr, "Wrote %zu bytes -> %s\n", res_len, out_path);
//  } else {
//    // stdout
//    fwrite(res, 1, res_len, stdout);
//    fflush(stdout);
//  }
//
//  free(blob);
//  free(res);
//  backend->vt->destroy(backend);
//  return 0;
//}
//
//

#include "src/epa_kernel_so.h"
#include "src/memory/epa_ghs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a64_bytes(const char *s) {
  uint64_t h = 1469598103934665603ull;
  while (*s) {
    h ^= (unsigned char)(*s++);
    h *= 1099511628211ull;
  }
  return h;
}

static uint8_t *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  uint8_t *buf;
  long n;
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
  buf = (uint8_t*)malloc((size_t)n ? (size_t)n : 1u);
  if (!buf) { fclose(f); return NULL; }
  if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_len = (size_t)n;
  return buf;
}

static EpaKernel *load_kernel(const char *path) {
  char err[EPA_MAX_ERR] = {0};
  EpaKernel *kernel = epa_kernel_create(err);
  if (!kernel) {
    fprintf(stderr, "create failed for %s: %s\n", path, err);
    return NULL;
  }
  if (!epa_kernel_load_asm(kernel, path, err)) {
    fprintf(stderr, "load failed for %s: %s\n", path, err);
    epa_kernel_destroy(kernel);
    return NULL;
  }
  return kernel;
}

static int expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
  }
  return 1;
}

int main(void) {
  char err[EPA_MAX_ERR] = {0};
  size_t importer_len = 0u;
  size_t dynlib_len = 0u;
  uint8_t *importer_bundle = read_file("demos/e_examples/build/tests/dynamic_import/importer.epa.bin", &importer_len);
  uint8_t *dynlib_bundle = read_file("demos/e_examples/build/tests/dynamic_import/dynlib.epa.bin", &dynlib_len);
  EpaKernel *authority = load_kernel("demos/e_examples/build/tests/dynamic_import/authority.epaasm");
  EpaKernelModule *importer_module = NULL;
  EpaKernelModule *imported_module = NULL;
  EpaKernel *importer = NULL;
  EpaKernel *imported_first = NULL;
  epa_ghs_handle_t h = 0u;
  uint32_t pid = 0u;
  uint32_t imported_count = 0u;
  uint64_t local_name = fnv1a64_bytes("modules.png");
  uint64_t expected_uid;
  int ok = 1;

  if (!authority || !importer_bundle || !dynlib_bundle) {
    fprintf(stderr, "failed to load dynamic import fixtures\n");
    ok = 0;
    goto done;
  }
  if (!epa_kernel_run(authority, 64, 0, err)) {
    fprintf(stderr, "authority run failed: %s\n", err);
    ok = 0;
    goto done;
  }

  importer_module = epa_kernel_process_load_bundle_bytes(authority, 1, importer_bundle, importer_len, 4301u, &pid, err);
  ok = ok && expect(importer_module && pid == 4301u, "privileged importer bundle loaded under requested PID");
  if (!ok || !importer_module) goto done;
  importer = epa_kernel_module_kernel(importer_module, 0);
  ok = ok && expect(importer && importer->impl.workers[1].privilege == 100u,
                    "importer worker has dynamic import privilege");

  imported_module = epa_kernel_process_import_dynamic_library_bytes(importer, 1, dynlib_bundle, dynlib_len, local_name, err);
  ok = ok && expect(imported_module && epa_kernel_module_count(imported_module) == 2u,
                    "dynamic import loads both bundle kernels");
  if (!ok || !imported_module) goto done;
  imported_first = epa_kernel_module_kernel(imported_module, 0);
  expected_uid = epa_kernel_namespace_uid(pid, local_name);
  ok = ok && expect(imported_first && imported_first->kernel_uid == expected_uid,
                    "first imported kernel is exposed under PID-local alias");
  ok = ok && expect(epa_kernel_resolve_uid_for_sender(importer, local_name) == expected_uid,
                    "sender resolves local dynamic library name through PID namespace");

  err[0] = 0;
  ok = ok && expect(epa_ghs_alloc(importer->impl.ghs, EPA_GHS_T_BYTES, 1u, (uint32_t)importer_len, &h) == EPA_GHS_OK,
                    "GHS blob allocated for dynamic import");
  ok = ok && expect(epa_ghs_write_bytes(importer->impl.ghs, h, 0u, importer_bundle, (uint32_t)importer_len) == EPA_GHS_OK,
                    "GHS blob populated for dynamic import");
  ok = ok && expect(epa_kernel_process_import_dynamic_library_ghs(importer, 1, h, (uint32_t)importer_len,
                                                                  fnv1a64_bytes("modules.png.ghs"),
                                                                  &imported_count, err),
                    "dynamic import works from a GHS handle");
  if (!ok && err[0]) fprintf(stderr, "dynamic import GHS error: %s\n", err);
  ok = ok && expect(imported_count == 1u, "GHS dynamic import reports imported module count");

done:
  if (importer_module) epa_kernel_module_destroy(importer_module);
  if (authority) epa_kernel_destroy(authority);
  free(importer_bundle);
  free(dynlib_bundle);
  if (!ok) return 1;
  printf("PASS e_examples dynamic import tests\n");
  return 0;
}

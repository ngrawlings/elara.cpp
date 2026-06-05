#include "src/epa_kernel_so.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static int expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
  }
  return 1;
}

int main(void) {
  char err[EPA_MAX_ERR] = {0};
  size_t bundle_len = 0u;
  uint8_t *bundle = read_file("demos/e_examples/build/tests/process_cluster_app.epa.bin", &bundle_len);
  uint32_t pid_a = 0u;
  uint32_t pid_b = 0u;
  uint32_t pid_c = 0u;
  int ok = 1;

  EpaKernel *authority = load_kernel("demos/e_examples/build/tests/process_cluster_authority.epaasm");
  EpaKernelModule *cluster_a = NULL;
  EpaKernelModule *cluster_b = NULL;
  EpaKernelModule *cluster_c = NULL;
  EpaKernel *a0;
  EpaKernel *a1;
  EpaKernel *b0;
  EpaKernel *c0;

  if (!bundle || !authority) {
    fprintf(stderr, "failed to read bundle or authority kernel\n");
    ok = 0;
    goto done;
  }

  if (!epa_kernel_run(authority, 64, 0, err)) {
    fprintf(stderr, "authority run failed: %s\n", err);
    ok = 0;
    goto done;
  }
  ok = ok && expect(authority->impl.workers[1].privilege == 100u,
                    "authority worker has process management privilege");

  cluster_a = epa_kernel_process_load_bundle_bytes(authority, 1, bundle, bundle_len, 4201u, &pid_a, err);
  ok = ok && expect(cluster_a && pid_a == 4201u, "load bundle under requested PID 4201");
  cluster_b = epa_kernel_process_load_bundle_bytes(authority, 1, bundle, bundle_len, 4202u, &pid_b, err);
  ok = ok && expect(cluster_b && pid_b == 4202u, "load same bundle under requested PID 4202");

  if (!ok || !cluster_a || !cluster_b) goto done;
  ok = ok && expect(epa_kernel_module_count(cluster_a) == 2u, "PID 4201 cluster has two kernels");
  ok = ok && expect(epa_kernel_module_count(cluster_b) == 2u, "PID 4202 cluster has two kernels");

  a0 = epa_kernel_module_kernel(cluster_a, 0);
  a1 = epa_kernel_module_kernel(cluster_a, 1);
  b0 = epa_kernel_module_kernel(cluster_b, 0);
  ok = ok && expect(a0 && a1 && b0, "cluster kernels are available");
  ok = ok && expect(epa_kernel_get_pid(a0) == 4201u && epa_kernel_get_pid(a1) == 4201u,
                    "all kernels in first cluster carry PID 4201");
  ok = ok && expect(epa_kernel_get_pid(b0) == 4202u,
                    "second cluster carries PID 4202");
  ok = ok && expect(epa_kernel_local_uid(a0) == epa_kernel_local_uid(b0),
                    "same app bundle keeps the same local kernel UID");
  ok = ok && expect(a0->kernel_uid != b0->kernel_uid,
                    "same app bundle gets different global UIDs per PID");
  ok = ok && expect(a0->kernel_uid == epa_kernel_namespace_uid(4201u, epa_kernel_local_uid(a0)),
                    "first global UID is derived from PID handle and local UID");
  ok = ok && expect(b0->kernel_uid == epa_kernel_namespace_uid(4202u, epa_kernel_local_uid(b0)),
                    "second global UID is derived from PID handle and local UID");
  ok = ok && expect(epa_kernel_resolve_uid_for_sender(a0, epa_kernel_local_uid(a1)) == a1->kernel_uid,
                    "child kernel resolves sibling local UID through its PID table");

  err[0] = 0;
  ok = ok && expect(epa_kernel_pid_retire(a0, 0, 4201u, err),
                    "child kernel can retire its own PID without privilege");
  ok = ok && expect(epa_kernel_get_status(a0) == EPA_KERNEL_STATUS_UNLOADED &&
                    epa_kernel_get_status(a1) == EPA_KERNEL_STATUS_UNLOADED,
                    "self PID retire unloads all kernels in the cluster");

  cluster_c = epa_kernel_process_load_bundle_bytes(authority, 1, bundle, bundle_len, 4203u, &pid_c, err);
  ok = ok && expect(cluster_c && pid_c == 4203u, "load third cluster for external kill test");
  if (!cluster_c) goto done;
  c0 = epa_kernel_module_kernel(cluster_c, 0);

  err[0] = 0;
  ok = ok && expect(!epa_kernel_pid_retire(b0, 0, 4203u, err),
                    "unprivileged child cannot retire another PID");
  ok = ok && expect(epa_kernel_get_status(c0) != EPA_KERNEL_STATUS_UNLOADED,
                    "failed external kill leaves target PID loaded");

  err[0] = 0;
  ok = ok && expect(epa_kernel_pid_retire(authority, 1, 4203u, err),
                    "privileged authority can retire another PID");
  ok = ok && expect(epa_kernel_get_status(c0) == EPA_KERNEL_STATUS_UNLOADED,
                    "privileged PID retire unloads target cluster");

done:
  if (cluster_c) epa_kernel_module_destroy(cluster_c);
  if (cluster_b) epa_kernel_module_destroy(cluster_b);
  if (cluster_a) epa_kernel_module_destroy(cluster_a);
  if (authority) epa_kernel_destroy(authority);
  free(bundle);

  if (!ok) return 1;
  printf("PASS e_examples process cluster PID tests\n");
  return 0;
}

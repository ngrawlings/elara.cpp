#include "src/epa_kernel_so.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint32_t opcode;
  uint32_t request_id;
  uint32_t input_size;
  uint32_t signature_lo;
  uint32_t signature_hi;
  uint32_t ihdr_length;
  uint32_t ihdr_type;
  uint32_t width;
  uint32_t height;
  uint32_t bit_depth;
  uint32_t color_type;
  uint32_t compression_method;
  uint32_t filter_method;
  uint32_t interlace_method;
} PngDecodeRequestWire;

static uint32_t rd32(const uint8_t *p) {
  return ((uint32_t)p[0]) |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 0;
  }
  return 1;
}

static int run_until_decoder_signal(EpaKernel *kernel, char err[EPA_MAX_ERR]) {
  for (int i = 0; i < 8; i++) {
    err[0] = 0;
    if (!epa_kernel_run(kernel, 100000, 0, err)) {
      return 0;
    }
    if (kernel->last_host_signal_wid == 1u && kernel->last_host_signal_len > 0u) {
      return 1;
    }
  }
  return 1;
}

static EpaKernelModule *load_png_decoder_module(EpaKernel **out_kernel) {
  char err[EPA_MAX_ERR] = {0};
  EpaKernelModule *module = epa_kernel_module_load_bundle("demos/elara-os/build/lib/png_decoder.epa.bin", err);
  EpaKernel *kernel;
  if (!module) {
    fprintf(stderr, "load png decoder bundle failed: %s\n", err);
    return NULL;
  }
  kernel = epa_kernel_module_kernel(module, 0);
  if (!kernel) {
    fprintf(stderr, "png decoder bundle has no kernel\n");
    epa_kernel_module_destroy(module);
    return NULL;
  }
  if (!epa_kernel_set_scheduler(kernel, EPA_SCHED_WAVE, err)) {
    fprintf(stderr, "set png decoder scheduler failed: %s\n", err);
    epa_kernel_module_destroy(module);
    return NULL;
  }
  if (!epa_kernel_run(kernel, 1000, 0, err)) {
    fprintf(stderr, "prime png decoder kernel failed: %s\n", err);
    epa_kernel_module_destroy(module);
    return NULL;
  }
  *out_kernel = kernel;
  return module;
}

int main(void) {
  char err[EPA_MAX_ERR] = {0};
  PngDecodeRequestWire ok_req = {
    2u, 77u, 67u,
    1196314761u, 169478669u,
    13u, 1380206665u,
    320u, 200u, 8u, 6u, 0u, 0u, 0u
  };
  PngDecodeRequestWire bad_req = ok_req;
  const uint8_t *mailbox;
  EpaKernel *kernel = NULL;
  EpaKernelModule *module = load_png_decoder_module(&kernel);
  int ok = 1;
  if (!module || !kernel) return 1;

  ok = ok && expect(epa_kernel_ingress_push(kernel, 1, &ok_req, sizeof(ok_req)),
                    "enqueue valid PNG header request");
  if (!run_until_decoder_signal(kernel, err)) {
    fprintf(stderr, "run valid request failed: %s\n", err);
    ok = 0;
  }
  mailbox = kernel->last_host_signal_bytes;
  ok = ok && expect(kernel->last_host_signal_wid == 1u, "decoder worker host-signaled valid request");
  ok = ok && expect(kernel->last_host_signal_len >= 60u, "decoder emitted PNG result mailbox");
  if (mailbox) {
    ok = ok && expect(rd32(mailbox + 0) == 0x45465231u, "result uses framed mailbox record");
    ok = ok && expect(rd32(mailbox + 12) == 1u, "valid PNG status is ok");
    ok = ok && expect(rd32(mailbox + 16) == 77u, "request id is preserved");
    ok = ok && expect(rd32(mailbox + 20) == 320u, "PNG width is preserved");
    ok = ok && expect(rd32(mailbox + 24) == 200u, "PNG height is preserved");
    ok = ok && expect(rd32(mailbox + 32) == 6u, "PNG color type is preserved");
    ok = ok && expect(rd32(mailbox + 36) == 8u, "PNG bit depth is preserved");
  }

  epa_kernel_module_destroy(module);
  kernel = NULL;
  module = load_png_decoder_module(&kernel);
  if (!module || !kernel) return 1;
  bad_req.signature_lo = 0u;
  ok = ok && expect(epa_kernel_ingress_push(kernel, 1, &bad_req, sizeof(bad_req)),
                    "enqueue invalid PNG header request");
  err[0] = 0;
  if (!run_until_decoder_signal(kernel, err)) {
    fprintf(stderr, "run invalid request failed: %s\n", err);
    ok = 0;
  }
  mailbox = kernel->last_host_signal_bytes;
  ok = ok && expect(mailbox && kernel->last_host_signal_len >= 16u,
                    "decoder emitted invalid PNG result mailbox");
  if (mailbox) {
    ok = ok && expect((int32_t)rd32(mailbox + 12) == -1,
                      "bad signature reports PNG signature error");
  }

  epa_kernel_module_destroy(module);
  if (!ok) return 1;
  printf("PASS elara-os PNG decoder dynamic library tests\n");
  return 0;
}

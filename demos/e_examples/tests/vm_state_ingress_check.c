#include "src/epa_kernel_so.h"

#include <stdint.h>
#include <stdio.h>

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

static int run_and_check(EpaKernel *target,
                         uint64_t expected_source_kernel_uid,
                         uint32_t expected_source_worker_id,
                         const char *label) {
  char err[EPA_MAX_ERR] = {0};
  const uint8_t *mailbox;
  int ok = 1;

  if (!epa_kernel_run(target, 128, 0, err)) {
    fprintf(stderr, "run failed for %s: %s\n", label, err);
    return 0;
  }

  mailbox = target->last_host_signal_bytes;
  ok = ok && expect(target->last_host_signal_wid == 1u, "receiver worker sent host signal");
  ok = ok && expect(target->last_host_signal_len >= 24u, "receiver emitted VM_STATE mailbox");
  if (!ok || !mailbox) return 0;

  ok = ok && expect(rd32(mailbox + 0) == (uint32_t)(target->kernel_uid & 0xFFFFFFFFu),
                    "VM_STATE current kernel uid low matches receiver");
  ok = ok && expect(rd32(mailbox + 4) == (uint32_t)(target->kernel_uid >> 32),
                    "VM_STATE current kernel uid high matches receiver");
  ok = ok && expect(rd32(mailbox + 8) == 1u,
                    "VM_STATE current worker id matches receiver worker");
  ok = ok && expect(rd32(mailbox + 12) == (uint32_t)(expected_source_kernel_uid & 0xFFFFFFFFu),
                    "VM_STATE source kernel uid low matches ingress frame");
  ok = ok && expect(rd32(mailbox + 16) == (uint32_t)(expected_source_kernel_uid >> 32),
                    "VM_STATE source kernel uid high matches ingress frame");
  ok = ok && expect(rd32(mailbox + 20) == expected_source_worker_id,
                    "VM_STATE source worker id matches ingress frame");
  return ok;
}

int main(void) {
  char err[EPA_MAX_ERR] = {0};
  uint32_t payload = 0xE1A5A5E1u;
  int ok = 1;

  EpaKernel *host_target = load_kernel("demos/e_examples/tests/vm_state_ingress_target.epaasm");
  if (!host_target) return 1;

  ok = ok && expect(epa_kernel_ingress_push(host_target, 1, &payload, sizeof(payload)),
                    "host can enqueue ingress");
  ok = ok && expect(host_target->ingress.inq[1].q[0].source_kernel_uid == EPA_HOST_KERNEL_UID,
                    "host ingress frame stores host kernel sentinel");
  ok = ok && expect(host_target->ingress.inq[1].q[0].source_worker_id == EPA_HOST_WORKER_ID,
                    "host ingress frame stores host worker sentinel");
  ok = ok && run_and_check(host_target, EPA_HOST_KERNEL_UID, EPA_HOST_WORKER_ID, "host ingress");
  epa_kernel_destroy(host_target);

  EpaKernel *target = load_kernel("demos/e_examples/tests/vm_state_ingress_target.epaasm");
  EpaKernel *sender = load_kernel("demos/e_examples/tests/vm_state_ingress_sender.epaasm");
  if (!target || !sender) {
    if (sender) epa_kernel_destroy(sender);
    if (target) epa_kernel_destroy(target);
    return 1;
  }

  err[0] = 0;
  ok = ok && expect(epa_kernel_far_signal_by_uid(sender, 1, target->kernel_uid, 1,
                                                  &payload, sizeof(payload), 0, err),
                    "worker can enqueue framed far ingress");
  ok = ok && expect(target->ingress.inq[1].q[0].source_kernel_uid == sender->kernel_uid,
                    "far ingress frame stores sender kernel uid");
  ok = ok && expect(target->ingress.inq[1].q[0].source_worker_id == 1u,
                    "far ingress frame stores sender worker id");
  ok = ok && run_and_check(target, sender->kernel_uid, 1u, "far ingress");

  epa_kernel_destroy(sender);
  epa_kernel_destroy(target);

  if (!ok) return 1;
  printf("PASS e_examples VM_STATE ingress provenance tests\n");
  return 0;
}

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

static int run_kernel(EpaKernel *kernel, const char *label) {
  char err[EPA_MAX_ERR] = {0};
  if (!epa_kernel_run(kernel, 64, 0, err)) {
    fprintf(stderr, "run failed for %s: %s\n", label, err);
    return 0;
  }
  return 1;
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
  uint32_t payload = 123u;
  int ok = 1;

  EpaKernel *privilege_entry = load_kernel("demos/e_examples/build/tests/privileged_workers_entry.epaasm");
  EpaKernel *security_entry = load_kernel("demos/e_examples/build/tests/dynamic_whitelist_entry.epaasm");
  EpaKernel *target = load_kernel("demos/e_examples/build/tests/dynamic_whitelist_target.epaasm");
  EpaKernel *app = load_kernel("demos/e_examples/build/tests/dynamic_whitelist_app.epaasm");

  if (!privilege_entry || !security_entry || !target || !app) {
    ok = 0;
    goto done;
  }

  ok = ok && run_kernel(privilege_entry, "privileged entry");
  ok = ok && expect(privilege_entry->impl.privilege_locked == 1u,
                    "entry kernel sealed worker privileges");
  ok = ok && expect(privilege_entry->impl.workers[0].privilege == 0u,
                    "kernel entry worker keeps default privilege");
  ok = ok && expect(privilege_entry->impl.workers[1].privilege == 100u,
                    "security_root received ACL admin privilege from entry.e static block");

  ok = ok && run_kernel(security_entry, "dynamic whitelist entry");
  ok = ok && expect(security_entry->impl.privilege_locked == 1u,
                    "dynamic whitelist entry sealed worker privileges");
  ok = ok && expect(security_entry->impl.workers[1].privilege == 100u,
                    "dynamic whitelist security_root received ACL admin privilege");

  ok = ok && expect(!epa_kernel_far_signal_by_uid(app, 1, target->kernel_uid, 1,
                                                   &payload, sizeof(payload), 0, err),
                    "app cannot signal closed target before dynamic grant");
  app->impl.workers[1].faulted = 0;
  app->impl.workers[1].fault_message[0] = 0;

  err[0] = 0;
  ok = ok && expect(epa_kernel_acl_grant_by_uid(security_entry, 1, target->kernel_uid,
                                                app->kernel_uid, 1, err),
                    "privileged worker can grant target route");
  ok = ok && expect(target->dynamic_acl_count == 1u,
                    "dynamic route stored on target kernel");

  err[0] = 0;
  ok = ok && expect(epa_kernel_far_signal_by_uid(app, 1, target->kernel_uid, 1,
                                                  &payload, sizeof(payload), 0, err),
                    "app can signal target after dynamic grant");
  ok = ok && expect(target->ingress.inq[1].count == 1u,
                    "dynamic grant delivered payload to target worker ingress");

  err[0] = 0;
  ok = ok && expect(epa_kernel_acl_revoke_by_uid(security_entry, 1, target->kernel_uid,
                                                 app->kernel_uid, 1, err),
                    "privileged worker can revoke target route");
  ok = ok && expect(target->dynamic_acl_count == 0u,
                    "single dynamic route removed from target kernel");

  err[0] = 0;
  ok = ok && expect(!epa_kernel_far_signal_by_uid(app, 1, target->kernel_uid, 1,
                                                   &payload, sizeof(payload), 0, err),
                    "app cannot signal target after route revoke");
  app->impl.workers[1].faulted = 0;
  app->impl.workers[1].fault_message[0] = 0;

  err[0] = 0;
  ok = ok && expect(epa_kernel_acl_grant_by_uid(security_entry, 1, target->kernel_uid,
                                                app->kernel_uid, 1, err),
                    "privileged worker can grant route before revoke_all");
  err[0] = 0;
  ok = ok && expect(epa_kernel_acl_revoke_all_by_uid(security_entry, 1, target->kernel_uid,
                                                     app->kernel_uid, err),
                    "privileged worker can revoke all app routes");
  ok = ok && expect(target->dynamic_acl_count == 0u,
                    "revoke_all cleared app dynamic routes");

done:
  if (app) epa_kernel_destroy(app);
  if (target) epa_kernel_destroy(target);
  if (security_entry) epa_kernel_destroy(security_entry);
  if (privilege_entry) epa_kernel_destroy(privilege_entry);

  if (!ok) return 1;
  printf("PASS e_examples privileged workers and dynamic whitelist tests\n");
  return 0;
}

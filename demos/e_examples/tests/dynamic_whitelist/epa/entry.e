type WhitelistCommand(int opcode, int target_worker) {
  return opcode;
}

kernel(VM vm) {
  kernalId("e.examples.tests.dynamic_whitelist.entry");

  static {
    set_worker_privilege(security_root, 100);
  }

  start_worker(security_root);
}

worker security_root(WhitelistCommand command) {
  grant_kernel_route("e.examples.tests.dynamic_whitelist.target", "e.examples.tests.dynamic_whitelist.app", 1);
  revoke_kernel_route("e.examples.tests.dynamic_whitelist.target", "e.examples.tests.dynamic_whitelist.app", 1);
  revoke_kernel_routes("e.examples.tests.dynamic_whitelist.target", "e.examples.tests.dynamic_whitelist.app");

  host_signal();
}

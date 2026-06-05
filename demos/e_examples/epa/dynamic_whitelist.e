type WhitelistCommand(int opcode, int app_uid_lo, int app_uid_hi, int target_worker) {
  return opcode;
}

kernel(VM vm) {
  kernalId("e.examples.dynamic_whitelist");

  static {
    set_worker_privilege(security_root, 100);
  }

  start_worker(security_root);
}

worker security_root(WhitelistCommand command) {
  grant_kernel_route("e.examples.dynamic_whitelist.target", "e.examples.dynamic_whitelist.app", 1);
  revoke_kernel_route("e.examples.dynamic_whitelist.target", "e.examples.dynamic_whitelist.app", 1);
  revoke_kernel_routes("e.examples.dynamic_whitelist.target", "e.examples.dynamic_whitelist.app");

  host_signal();
}

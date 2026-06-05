type PrivilegeProbe(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("e.examples.tests.privileged_workers.entry");

  static {
    set_worker_privilege(security_root, 100);
  }

  start_worker(security_root);
}

worker security_root(PrivilegeProbe probe) {
  host_signal();
}

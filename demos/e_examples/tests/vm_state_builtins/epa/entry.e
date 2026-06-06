type VmStatePing(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("e.examples.vm_state_builtins");
  start_worker(receiver);
}

acl {
  "e.examples.host" -> receiver;
}

worker receiver(VmStatePing ping) {
  int current_kernel_lo = current_kernel_uid_low();
  int current_kernel_hi = current_kernel_uid_high();
  int current_worker = current_worker_id();
  int source_kernel_lo = ingress_source_kernel_uid_low();
  int source_kernel_hi = ingress_source_kernel_uid_high();
  int source_worker = ingress_source_worker_id();

  host_signal();
}

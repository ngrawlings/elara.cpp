type HelperCommand(int opcode) {
  return opcode;
}

kernel(VM vm) {
  kernalId("e.examples.tests.process_cluster.helper");
  start_worker(helper_worker);
}

worker helper_worker(HelperCommand command) {
  host_signal();
}

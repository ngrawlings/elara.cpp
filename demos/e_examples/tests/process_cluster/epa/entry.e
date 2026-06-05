type AppCommand(int opcode) {
  return opcode;
}

kernel(VM vm) {
  kernalId("e.examples.tests.process_cluster.app");
  start_worker(app_worker);
}

worker app_worker(AppCommand command) {
  host_signal();
}

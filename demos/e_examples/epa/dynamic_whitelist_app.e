type WhitelistAppMessage(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("e.examples.dynamic_whitelist.app");
  start_worker(app_worker);
}

worker app_worker(WhitelistAppMessage message) {
  host_signal();
}

type WhitelistTargetMessage(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("e.examples.tests.dynamic_whitelist.target");
  start_worker(target_ingress);
}

acl {
  "e.examples.tests.dynamic_whitelist.other" -> target_ingress;
}

worker target_ingress(WhitelistTargetMessage message) {
  host_signal();
}

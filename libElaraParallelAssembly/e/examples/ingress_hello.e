type GreetingIngress(int day_phase, int greeting_id) {
  // Validator body placeholder.
  return day_phase;
}

kernel(VM vm) {
  kernalId("example.ingress_hello");
  // Pipeline sketch:
  // 1. data ingress creates the underlying GHS block for GreetingIngress
  // 2. kernel routes that typed block to the hello worker
  ingress_greeting(vm, hello_world);
}

acl {
  "example.remote" -> hello_world;
}

worker hello_world(GreetingIngress ingress) {
  if (ingress.day_phase) {
    emit_phrase(1);
  } else {
    emit_phrase(2);
  }
}

function int emit_phrase(int phrase_id) {
  return phrase_id;
}

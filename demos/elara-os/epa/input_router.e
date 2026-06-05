type InputEvent(int kind, int code, int value, int target_surface) {
  return kind;
}

type InputRoute(int surface_id, int owner_uid_lo, int owner_uid_hi, int flags) {
  return surface_id;
}

dynamic routes(InputRoute, 4, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.input");
  start_worker(input_ingress);
}

acl {
  "elara.os.host" -> input_ingress;
  "elara.os.shell" -> input_ingress;
}

worker input_ingress(InputEvent event) {
  int route = dynamic_iterator(routes);

  host_signal();
}

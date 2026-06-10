type HidInputEvent(int kind, int code, int value, int target_surface) {
  return kind;
}

type HidInputRoute(int surface_id, int owner_uid_lo, int owner_uid_hi, int flags) {
  return surface_id;
}

dynamic routes(HidInputRoute, 4, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.hid_io");
  start_worker(hid_ingress);
}

acl {
  "elara.os.host" -> hid_ingress;
  "elara.os.shell" -> hid_ingress;
}

worker hid_ingress(HidInputEvent event) {
  int route = dynamic_iterator(routes);

  host_signal();
}

type AppEvent(int opcode, int surface_id, int arg0, int arg1) {
  return opcode;
}

type AppState(int surface_id, int frame_counter, int flags, int reserved) {
  return surface_id;
}

kernel(VM vm) {
  kernalId("elara.app.example");
  start_worker(app_ingress);
}

acl {
  "elara.os.shell" -> app_ingress;
  "elara.os.hid_io" -> app_ingress;
}

worker app_ingress(AppEvent event) {
  static AppState state;

  host_signal();
}

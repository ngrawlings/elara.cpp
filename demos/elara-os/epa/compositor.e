type SurfaceRequest(int opcode, int surface_id, int arg0, int arg1) {
  return opcode;
}

type SurfaceState(int surface_id, int owner_uid_lo, int owner_uid_hi, int flags) {
  return surface_id;
}

dynamic surfaces(SurfaceState, 8, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.compositor");
  start_worker(surface_ingress);
}

acl {
  "elara.os.shell" -> surface_ingress;
  "elara.app.example" -> surface_ingress;
}

worker surface_ingress(SurfaceRequest request) {
  int slot = dyn_alloc(surfaces);

  host_signal();
}

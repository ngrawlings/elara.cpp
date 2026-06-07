type ShellEvent(int opcode, int target_id, int arg0, int arg1) {
  return opcode;
}

type ShellSurface(int surface_id, int x, int y, int flags) {
  return surface_id;
}

dynamic shell_surfaces(ShellSurface, 4, 128, 8);

kernel(VM vm) {
  kernalId("elara.os.shell");
  start_worker(shell_ingress);
}

acl {
  "elara.os.input" -> shell_ingress;
  "elara.os.frame_authority" -> shell_ingress;
  "elara.app.example" -> shell_ingress;
}

worker shell_ingress(ShellEvent event) {
  int surface = dyn_alloc(shell_surfaces);

  host_signal();
}

type BootCommand(int opcode, int arg0, int arg1, int arg2) {
  return opcode;
}

type BootAssets(int version, int flags) {
  return version;
}

kernel(VM vm) {
  kernalId("elara.os.boot");

  start_worker(publish_boot_assets);
  retire_worker(publish_boot_assets);

  retire_kernel("elara.os.boot");
}

worker publish_boot_assets(BootCommand command) {
  static BootAssets assets;

  static {
    rgm_publish("elara.os.boot.assets", assets);
    retire_worker();
  }
}

type IoRequest(int opcode, int channel_id, int arg0, int arg1) {
  return opcode;
}

type IoCompletion(int request_id, int status, int result_handle_lo, int result_handle_hi) {
  return status;
}

kernel(VM vm) {
  kernalId("elara.os.io");
  start_worker(io_ingress);
}

acl {
  "elara.os.boot" -> io_ingress;
  "elara.os.storage" -> io_ingress;
  "elara.os.network" -> io_ingress;
}

worker io_ingress(IoRequest request) {
  host_signal();
}

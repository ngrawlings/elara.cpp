type NetworkRequest(int opcode, int endpoint_id, int arg0, int arg1) {
  return opcode;
}

type NetworkEndpoint(int endpoint_id, int protocol, int flags, int owner_uid_lo) {
  return endpoint_id;
}

dynamic endpoints(NetworkEndpoint, 4, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.network");
  start_worker(network_ingress);
}

acl {
  "elara.os.shell" -> network_ingress;
  "elara.app.example" -> network_ingress;
}

worker network_ingress(NetworkRequest request) {
  int endpoint = dyn_alloc(endpoints);

  host_signal();
}

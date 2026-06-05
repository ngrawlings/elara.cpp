type StorageRequest(int opcode, int object_id, int offset, int length) {
  return opcode;
}

type StorageObject(int object_id, int flags, int size_lo, int size_hi) {
  return object_id;
}

dynamic objects(StorageObject, 8, 128, 16);

kernel(VM vm) {
  kernalId("elara.os.storage");
  start_worker(storage_ingress);
}

acl {
  "elara.os.shell" -> storage_ingress;
  "elara.app.example" -> storage_ingress;
}

worker storage_ingress(StorageRequest request) {
  int object_iter = dynamic_iterator(objects);

  host_signal();
}

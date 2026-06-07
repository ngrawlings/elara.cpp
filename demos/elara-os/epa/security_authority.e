type SecurityRequest(int opcode, int subject_uid_lo, int subject_uid_hi, int object_id) {
  return opcode;
}

type GrantRecord(int subject_uid_lo, int subject_uid_hi, int object_id, int rights) {
  return rights;
}

dynamic grants(GrantRecord, 16, 256, 16);

kernel(VM vm) {
  kernalId("elara.os.security");
  start_worker(security_ingress);
}

acl {
  "elara.os.boot" -> security_ingress;
  "elara.os.shell" -> security_ingress;
  "elara.os.frame_authority" -> security_ingress;
  "elara.os.storage" -> security_ingress;
}

worker security_ingress(SecurityRequest request) {
  int grant_iter = dynamic_iterator(grants);

  host_signal();
}

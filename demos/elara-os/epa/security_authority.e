#include "dynamic_acl_protocol.em"

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
  static int registered;
  local DynamicACLRequest acl_request;
  int grant_iter = dynamic_iterator(grants);

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_security();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  host_signal();
}

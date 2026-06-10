#include "../../dynamic_acl_protocol.em"

type StreamEndpointRegistration(
  int stream_id,
  int protocol,
  int flags,
  int owner_authority,
  int endpoint_kind,
  int target_size,
  byte[] target_data
) {
  return stream_id;
}

type StreamIoRequest(
  int opcode,
  int stream_id,
  int arg0,
  int arg1,
  int payload_size,
  byte[] payload_data
) {
  return opcode;
}

type StreamIoStatus(int stream_id, int status, int arg0, int arg1) {
  return status;
}

type StreamEndpointState(
  int stream_id,
  int protocol,
  int flags,
  int owner_authority,
  int endpoint_kind,
  int target_size
) {
  return stream_id;
}

dynamic stream_endpoints(StreamEndpointState, 4, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.stream_io");
  start_worker(register_stream_endpoint);
  start_worker(stream_io_ingress);
}

acl {
  "elara.os.boot" -> register_stream_endpoint;
  "elara.os.network" -> register_stream_endpoint;
  "elara.os.network" -> stream_io_ingress;
  "elara.os.shell" -> stream_io_ingress;
  "elara.app.example" -> stream_io_ingress;
}

worker register_stream_endpoint(StreamEndpointRegistration registration) {
  static int registered_count;
  static int registered;
  int slot = dyn_alloc(stream_endpoints);
  local StreamEndpointState endpoint;
  local StreamEndpointRegistration staged;
  local DynamicACLRequest acl_request;

  static {
    registered_count = 0;
  }

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_stream_io();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  staged.stream_id = registration.stream_id;
  staged.protocol = registration.protocol;
  staged.flags = registration.flags;
  staged.owner_authority = registration.owner_authority;
  staged.endpoint_kind = registration.endpoint_kind;
  staged.target_size = registration.target_size;
  staged.target_data = registration.target_data;

  endpoint.stream_id = staged.stream_id;
  endpoint.protocol = staged.protocol;
  endpoint.flags = staged.flags;
  endpoint.owner_authority = staged.owner_authority;
  endpoint.endpoint_kind = staged.endpoint_kind;
  endpoint.target_size = staged.target_size;
  stream_endpoints[slot] = endpoint;

  registered_count = registered_count + 1;
  host_signal();
}

worker stream_io_ingress(StreamIoRequest request) {
  int endpoint_iter = dynamic_iterator(stream_endpoints);
  endpoint_iter = endpoint_iter;
  host_signal();
}

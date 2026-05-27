type DemoIngress(int seq, int left_value, int right_value) {
  return seq;
}

type RemotePayload(int seq, int sum_value, int route_code) {
  return seq;
}

type RemoteAck(int seq, int echoed_sum, int stage_code) {
  return seq;
}

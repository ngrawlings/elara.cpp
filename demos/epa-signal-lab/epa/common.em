type DemoIngress(int seq, int left_value, int right_value) {
  return seq;
}

// Trigger payload for the intruder kernel's unauthorized far_signal attempt.
type IntruderTrigger(int attempt_id) {
  return attempt_id;
}

type RemotePayload(int seq, int sum_value, int route_code) {
  return seq;
}

type RemoteAck(int seq, int echoed_sum, int stage_code) {
  return seq;
}

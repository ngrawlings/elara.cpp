#include "common.em"

declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

kernel(VM vm) {
  kernalId("demo.signal_lab.remote");
  start_worker(remote_ingress);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      RemotePayload inbound = kernal_get_ghs(1);
      // Remote kernel observed an inbound far_signal payload.
      inbound.seq = inbound.seq;
    }
  }
}

// Only the entry kernel is whitelisted. Any other kernel that attempts
// far_signal into remote_ingress will be hard-faulted by the ACL check.
acl {
  "demo.signal_lab.entry" -> remote_ingress;
}

worker remote_ingress(RemotePayload inbound) {
  local RemoteAck ack;

  ack.seq = inbound.seq;
  ack.echoed_sum = inbound.sum_value;
  ack.stage_code = inbound.route_code + 1;

  // Remote kernel -> original kernel typed payload.
  far_signal("demo.signal_lab.entry", remote_ack_receiver, ack);

  // Also signal the remote kernel so it can inspect its current GHS.
  signal();
}

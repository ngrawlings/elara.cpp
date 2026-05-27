#include "common.em"

declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

kernel(VM vm) {
  kernalId("demo.signal_lab.entry");
  ingress_source(vm);
  local_forward(vm);
  remote_ack_receiver(vm);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 2) {
      DemoIngress seen = kernal_get_ghs(2);
      // Local worker handoff reached the kernel through signal().
      seen.seq = seen.seq;
    } else if (wid == 3) {
      RemoteAck ack = kernal_get_ghs(3);
      // Remote kernel returned a typed ack through far_signal().
      ack.seq = ack.seq;
    }
  }
}

acl {
  "demo.signal_lab.remote" -> remote_ack_receiver;
}

worker ingress_source(DemoIngress ingress) {
  // `next` forwards the current worker GHS to another local worker.
  next local_forward;
}

worker local_forward(DemoIngress ingress) {
  local RemotePayload outbound;

  outbound.seq = ingress.seq;
  outbound.sum_value = ingress.left_value + ingress.right_value;
  outbound.route_code = 200 + ingress.seq;

  // Local worker -> kernel signal. The kernel can read this worker's current GHS.
  signal();

  // Local worker -> remote kernel typed payload.
  far_signal("demo.signal_lab.remote", outbound);

  // Worker -> host mailbox payload. The host decodes the frame words.
  frame_begin(ingress.seq, ingress.left_value, ingress.right_value, outbound.sum_value, outbound.route_code);
  frame_commit();
}

worker remote_ack_receiver(RemoteAck ack) {
  // Remote kernel returned data to this local worker. Signal the kernel so it can inspect the ack.
  signal();
}

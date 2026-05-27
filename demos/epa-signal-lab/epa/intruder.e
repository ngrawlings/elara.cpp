#include "common.em"

declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

// Intruder kernel — intentional ACL fault demo.
//
// Data life cycle (faults):
//   Host injects IntruderTrigger into intrude_attempt
//   intrude_attempt builds a RemotePayload and calls far_signal into
//     demo.signal_lab.remote / remote_ingress
//   remote_sink's ACL only whitelists demo.signal_lab.entry, so the call
//     is rejected at runtime and intrude_attempt is hard-faulted.
//   The worker cannot run again until the VM is reset.
//
// Compare with the entry/remote_sink round-trip (data life cycle that succeeds):
//   Host injects DemoIngress into entry's ingress_source
//   ingress_source -> local_forward (next)
//   local_forward -> far_signal -> remote_ingress  (whitelisted: succeeds)
//   remote_ingress -> far_signal -> remote_ack_receiver (whitelisted: succeeds)

kernel(VM vm) {
  kernalId("demo.signal_lab.intruder");
  intrude_attempt(vm);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // coordinator — intruder has no outbound product
  }
}

// No ACL: this kernel accepts no far_signal from external kernels.

worker intrude_attempt(IntruderTrigger trigger) {
  local RemotePayload payload;

  // Build a plausible-looking payload.
  payload.seq = trigger.attempt_id;
  payload.sum_value = 0;
  payload.route_code = 999;

  // This far_signal targets demo.signal_lab.remote whose ACL only allows
  // demo.signal_lab.entry. The runtime rejects this call and hard-faults
  // this worker with: "ACL FAULT: kernel 'demo.signal_lab.intruder' is
  // not whitelisted by target kernel 0x<uid>".
  far_signal("demo.signal_lab.remote", remote_ingress, payload);

  // Unreachable — the far_signal above terminates this worker with a fault.
  signal();
}

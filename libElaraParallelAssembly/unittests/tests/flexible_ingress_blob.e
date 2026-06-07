#include "common/bytes.em"

type BlobIngress(int kind, int size, byte[] data) {
  return kind;
}

kernel(VM vm) {
  kernalId("test.flexible.ingress.blob");
  start_worker(main);
}

worker main(BlobIngress msg) {
  local BlobIngress staged;
  local byte[12] payload;

  local_store_i32(payload, 0, 67305985);
  local_store_i32(payload, 4, 134678021);
  local_store_i32(payload, 8, 202050057);

  staged.kind = 7;
  staged.size = 12;
  staged.data = payload;

  int staged_kind = staged.kind;
  int staged_size = staged.size;
  int staged_word0 = local_load_i32(staged.data, 0);
  int staged_word1 = local_load_i32(staged.data, 4);
  int staged_word2 = local_load_i32(staged.data, 8);

  staged.kind = msg.kind;
  staged.size = msg.size;
  staged.data = msg.data;

  int ingress_kind = staged.kind;
  int ingress_size = staged.size;
  int ingress_word0 = local_load_i32(staged.data, 0);
  int msg_word0 = local_load_i32(msg.data, 0);
}

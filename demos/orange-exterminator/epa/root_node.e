declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

struct RootNodePayload;
struct EPABlob;
struct KeyInput;

type RootNodePayload(int tag) {
  return tag;
}

type EPABlob(int blob_id) {
  return blob_id;
}

type KeyInput(int key_code) {
  return key_code;
}

kernel(VM vm) {
  root_node_worker(vm);
  root_node_child_kernel_worker(vm);
  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      RootNodePayload payload = kernal_get_ghs(1);
      // TODO: integrate the worker payload into kernel context/state here.
    } else if (wid == 2) {
      // TODO: integrate child-kernel coordination or artifact state here.
    } else {
      // TODO: handle additional worker signals here.
    }
  }
}

worker root_node_worker(RootNodePayload payload) {
  // TODO: add worker logic here.
  // TODO: call kernel_signal() after updating the worker payload for kernel integration.
  kernel_signal();
}

worker root_node_child_kernel_worker(EPABlob|KeyInput ingress) {
  int ingress_kind = typeof(ingress);
  if (ingress_kind == typeid(EPABlob)) {
    // TODO: load or refresh a child kernel from the incoming EPA blob here.
  } else if (ingress_kind == typeid(KeyInput)) {
    // TODO: forward key input into the running child kernel here.
  } else {
    // TODO: handle additional child-kernel ingress payload types here.
  }
  // TODO: publish any child-kernel artifact needed by the parent kernel before kernel_signal().
  kernel_signal();
}

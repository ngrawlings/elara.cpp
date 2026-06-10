type FrameBoot(int phase, int flags) {
  return phase;
}

type FrameRequest(int opcode, int surface_id, int arg0, int arg1) {
  return opcode;
}

type FrameManagerFocus(int manager_id, int reason, int flags, int reserved) {
  return manager_id;
}

type FrameManagerFrame(
  int manager_id,
  int frame_id,
  int frame_kind,
  int flags,
  int arg0,
  int arg1,
  int arg2,
  int arg3
) {
  return manager_id;
}

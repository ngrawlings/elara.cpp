type SliceTrigger(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.dynamic.slice.local");
  start_worker(main);
}

worker main(SliceTrigger trigger) {
  dynamic chunks(byte[4], 4, 6, 4);
  int a = dyn_alloc(chunks);
  int b = dyn_alloc(chunks);
  int c = dyn_alloc(chunks);
  local byte[8] middle = chunks[1:3];
  dyn_free(chunks, c);
  dyn_free(chunks, b);
  dyn_free(chunks, a);
}

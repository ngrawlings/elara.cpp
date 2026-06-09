type EntryTick(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("elara.os.entry");
  start_worker(entry_idle);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    wid = wid;
  }
}

worker entry_idle(EntryTick tick) {
  signal();
}

type DynamicCell(int id, int value) {
  return id;
}

dynamic cells(DynamicCell, 2, 6, 4);

kernel(VM vm) {
  kernalId("e.examples.dynamic_memory");

  int first = dyn_alloc(cells);
  int last = first;
  int index = 1;

  while (index < 40) {
    last = dyn_alloc(cells);
    index = index + 1;
  }

  int it = dynamic_iterator(cells);
  int seen = 0;
  while (DynamicCell cell = dynamic_next(it)) {
    seen = seen + 1;
  }

  dyn_free(cells, last);
  dyn_free(cells, first);
}

worker unused_probe(DynamicCell trigger) {
  signal();
}

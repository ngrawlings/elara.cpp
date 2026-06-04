type MultiplicationTable(int[10000] cells) {
  return 0;
}

kernel(VM vm) {
  kernalId("e.examples.multiplication_table");

  local MultiplicationTable initial_payload;
  int seed_index = 0;
  while (seed_index < 10000) {
    local_store_i32(initial_payload, seed_index * 4, 0 - 1);
    seed_index = seed_index + 1;
  }

  MultiplicationTable table = ghs_alloc_from_local(MultiplicationTable, initial_payload);

  request_at(build_cell, table, 10000, 16);
  start_worker(table_sink);
}

acl {
  "e.examples.host" -> table_sink;
}

worker table_sink(MultiplicationTable table) {
  host_signal();
}

at_entry build_cell(u32 thread_index, MultiplicationTable table) {
  int row = thread_index / 100;
  int col = thread_index - (row * 100);
  int lhs = row + 1;
  int rhs = col + 1;
  int value = lhs * rhs;
  int byte_offset = thread_index * 4;

  ghs_store_i32(table, byte_offset, value);
}

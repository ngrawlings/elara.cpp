// dynamic_memory.e
// Fuzzes the dynamic pool lifecycle across two workers with independent pool instances.
//
// stress_worker: alloc 3 / swap pairs / iterate / free LIFO each round.
// grow_worker:   alternates between filling all 4 slots (phase 0) and
//                iterating + freeing all (phase 1), exercising pool growth
//                and shrink at round boundaries.
//
// Run with e_fuzz_runner; the runner distributes ingress to wid 1 and wid 2.
// Each worker owns its own pool to avoid interleaving conflicts.

type Cell(int id, int val) {
    return id;
}

kernel(VM vm) {
    kernalId("test.dynamic_memory");
    start_worker(stress_worker);
    start_worker(grow_worker);

    int wid = 0;
    while (1) {
        wid = kernel_wait_signal();
    }
}

// Worker 1: alloc 3, swap twice, count via iteration, free LIFO.
// Exercises: DYN_ALLOC, DYN_SWAP, DYN_ITER_HEAD/NEXT, DYN_FREE (swap-with-tail path).
worker stress_worker(Cell trigger) {
    dynamic stress_cells(Cell, 4, 6, 4);
    static int round_n;

    static {
        round_n = 100;
    }

    round_n = round_n + 1;

    int a = dyn_alloc(stress_cells);
    int b = dyn_alloc(stress_cells);
    int c = dyn_alloc(stress_cells);

    dyn_swap(stress_cells, a, b);
    dyn_swap(stress_cells, b, c);

    int it = dynamic_iterator(stress_cells);
    int seen = 0;
    while (Cell item = dynamic_next(it)) {
        seen = seen + 1;
    }

    dyn_free(stress_cells, c);
    dyn_free(stress_cells, b);
    dyn_free(stress_cells, a);

    signal();
}

// Worker 2: two-phase pattern that forces pool growth then shrink.
// Phase 0: fill pool to capacity (4 items), persist ordinals across rounds.
// Phase 1: iterate live items, free all 4 LIFO, triggering pool shrink on next entry.
worker grow_worker(Cell trigger) {
    dynamic grow_cells(Cell, 4, 6, 4);
    static int phase;
    static int ord0;
    static int ord1;
    static int ord2;
    static int ord3;

    static {
        phase = 0;
    }

    if (phase == 0) {
        ord0 = dyn_alloc(grow_cells);
        ord1 = dyn_alloc(grow_cells);
        ord2 = dyn_alloc(grow_cells);
        ord3 = dyn_alloc(grow_cells);
        phase = 1;
    } else {
        int it = dynamic_iterator(grow_cells);
        int seen = 0;
        while (Cell item = dynamic_next(it)) {
            seen = seen + 1;
        }
        dyn_free(grow_cells, ord3);
        dyn_free(grow_cells, ord2);
        dyn_free(grow_cells, ord1);
        dyn_free(grow_cells, ord0);
        phase = 0;
    }

    signal();
}

// exec_type values (adjust names to match your codebase)
#ifndef EPA_EXEC_WORKER
#define EPA_EXEC_WORKER   0
#endif
#ifndef EPA_EXEC_AT_THREAD
#define EPA_EXEC_AT_THREAD 1
#endif

// Protect worker-only opcodes.
// Usage inside an opcode handler that has `w` and `err` in scope:
//
//   EPA_REQUIRE_WORKER_ONLY(w, err, "SM_PUT", EPA_FLOW_ERR);
//
// The last argument is the return value for the current function
// (e.g. EPA_FLOW_ERR, EPA_NF_EXEC_ERR, 0, etc.).
#define EPA_REQUIRE_WORKER_ONLY(w, err, opname, retcode)                   \
  do {                                                                      \
    if ((w) && (w)->exec_type == EPA_EXEC_AT_THREAD) {                      \
      if ((err)) {                                                          \
        snprintf((err), EPA_MAX_ERR, "%s: worker-only opcode", (opname));   \
      }                                                                     \
      return (retcode);                                                     \
    }                                                                       \
  } while (0)

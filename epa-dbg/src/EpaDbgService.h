#ifndef EPADBGSERVICE_H
#define EPADBGSERVICE_H

#include <deque>
#include <vector>
#include <map>
#include <string>
#include <libelaracore/memory/String.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include "EpaDbgVmHost.h"
#include "EpaDbgShim.h"

typedef void (*EpaDbgCallback)(void *user, int kind, uint8_t wid, uint32_t code, const EpaDbgEip *at, const char *msg);

namespace elara {

class EpaDbgService : public sockets::rpc::json::JsonRPCService {
public:
    EpaDbgService();
    virtual ~EpaDbgService();

    virtual bool call(const String &method, const String &params_json,
                      String &result_json, String &error_code, String &error_message);

private:
    struct DebugEvent {
        String   kind;
        uint32_t wid;
        uint32_t code;
        uint8_t  block_type;
        uint32_t block_id;
        uint32_t rel_pc;
        String   message;
    };
    struct Breakpoint {
        uint8_t  block_type;
        uint32_t block_id;
        uint32_t rel_pc;
    };
    struct MapEntry {
        uint32_t offset;
        int      epa_line;
        int      e_line;
        int      epa_col;
    };
    struct WorkerMarkerState {
        bool     valid;
        uint32_t wid;
        uint8_t  block_type;
        uint32_t block_id;
        uint32_t rel_pc;
        int      epa_line;
        int      e_line;
        int      epa_col;
        bool     waiting_for_data;
        bool     halted;
        bool     faulted;
    };

    EpaDbgVmHost            host;
    std::deque<DebugEvent>  events;
    std::vector<Breakpoint> breakpoints;
    std::map<std::string, bool> worker_debug_enabled;
    uint64_t                next_event_id;

    static void onKernelDebug(void *user, int kind, uint8_t wid, uint32_t code,
                               const EpaDbgEip *at, const char *msg);
    void ensureDebugCallback();
    void pushEvent(const String &kind, uint32_t wid, uint32_t code,
                   const EpaDbgEip *at, const String &message);
    void pushLog(const String &message);

    bool runTicks(const String &path_id, uint32_t tick_count, bool stop_on_breakpoint,
                  uint32_t target_wid,
                  String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool runToWait(const String &path_id, uint32_t target_wid, uint32_t max_ticks,
                   String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool stepBoundary(const String &path_id, const String &map_path, uint32_t target_wid,
                      const String &step_mode, uint32_t max_ticks,
                      String &stop_reason, uint32_t &ticks_ran,
                      WorkerMarkerState &out_state, String &error_message);
    bool hasBreakpointHit(uint32_t *out_wid, Breakpoint *out_bp) const;
    EpaKernel *activeKernel() const;
    EpaKernel *kernelForPath(const String &path_id) const;
    std::string workerDebugKey(const String &path_id, uint32_t wid) const;
    bool workerDebugIsEnabled(const String &path_id, uint32_t wid) const;
    void setWorkerDebugEnabled(const String &path_id, uint32_t wid, bool enabled);

    String buildSnapshotJson(const String &path_id = String()) const;
    String buildEventsJson(bool clear_after_read);
    String buildBreakpointJson() const;
    String buildMarkerJson(const WorkerMarkerState &state) const;
    String buildWorkerInspectJson(const String &path_id, uint32_t wid,
                                  uint32_t stack_words, uint32_t arena_bytes,
                                  uint32_t ghs_bytes, String &error_message) const;

    bool parseUint(const String &json, const String &field, uint32_t def, uint32_t *out) const;
    bool parseBool(const String &json, const String &field, bool def, bool *out) const;
    bool parseString(const String &json, const String &field, String &out) const;
    bool parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const;
    bool loadEpaMap(const String &map_path, std::map<std::string, std::vector<MapEntry> > &out_map, String &error_message) const;
    std::string blockKey(uint8_t block_type, uint32_t block_id) const;
    WorkerMarkerState markerStateForWorker(EpaKernel *kernel, const std::map<std::string, std::vector<MapEntry> > &map, uint32_t target_wid) const;
    bool boundaryCrossed(bool use_epa_mode, const WorkerMarkerState &before, const WorkerMarkerState &after) const;
    String stalledReason(bool use_epa_mode, const WorkerMarkerState &before, const WorkerMarkerState &after) const;
};

} // namespace elara

#endif

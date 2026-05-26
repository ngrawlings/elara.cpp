#ifndef EPADBGSERVICE_H
#define EPADBGSERVICE_H

#include <deque>
#include <vector>
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

    EpaDbgVmHost            host;
    std::deque<DebugEvent>  events;
    std::vector<Breakpoint> breakpoints;
    uint64_t                next_event_id;

    static void onKernelDebug(void *user, int kind, uint8_t wid, uint32_t code,
                               const EpaDbgEip *at, const char *msg);
    void ensureDebugCallback();
    void pushEvent(const String &kind, uint32_t wid, uint32_t code,
                   const EpaDbgEip *at, const String &message);
    void pushLog(const String &message);

    bool runTicks(uint32_t tick_count, bool stop_on_breakpoint,
                  String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool runToWait(uint32_t target_wid, uint32_t max_ticks,
                   String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool hasBreakpointHit(uint32_t *out_wid, Breakpoint *out_bp) const;

    String buildSnapshotJson(const String &path_id = String()) const;
    String buildEventsJson(bool clear_after_read);
    String buildBreakpointJson() const;

    bool parseUint(const String &json, const String &field, uint32_t def, uint32_t *out) const;
    bool parseBool(const String &json, const String &field, bool def, bool *out) const;
    bool parseString(const String &json, const String &field, String &out) const;
    bool parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const;
};

} // namespace elara

#endif

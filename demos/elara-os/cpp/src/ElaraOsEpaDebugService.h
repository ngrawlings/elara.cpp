#ifndef ELARAOSEPADEBUGSERVICE_H
#define ELARAOSEPADEBUGSERVICE_H

#include <deque>
#include <map>
#include <string>
#include <vector>
#include <libelaracore/memory/String.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include "ElaraOsEpaVmHost.h"
#include "ElaraOsEpaDebugShim.h"

typedef struct {
    uint8_t block_type;
    uint16_t block_id;
    uint32_t rel_pc;
} ElaraOsDbgEip;

typedef void (*ElaraOsKernelDbgCallback)(void *cb_user, int kind, uint8_t wid, uint32_t code, const ElaraOsDbgEip *at, const char *msg);

namespace elara {

class ElaraOsEpaDebugService : public sockets::rpc::json::JsonRPCService {
public:
    ElaraOsEpaDebugService();
    virtual ~ElaraOsEpaDebugService();
    virtual bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);

private:
    struct DebugEvent {
        String kind;
        uint32_t wid;
        uint32_t code;
        uint8_t block_type;
        uint16_t block_id;
        uint32_t rel_pc;
        String message;
    };
    struct Breakpoint {
        uint8_t block_type;
        uint16_t block_id;
        uint32_t rel_pc;
    };

    ElaraOsEpaVmHost host;
    std::deque<DebugEvent> events;
    std::vector<Breakpoint> breakpoints;
    uint64_t next_event_id;
    std::vector<uint8_t> last_mailbox_bytes;
    uint8_t last_mailbox_wid;
    std::map<std::string, bool> worker_debug_enabled;

    static void onKernelDebug(void *cb_user, int kind, uint8_t wid, uint32_t code, const ::ElaraOsDbgEip *at, const char *msg);
    static int onSignalMailbox(uint8_t wid, const char *data, const int len);
    void ensureDebugCallbackInstalled();
    void ensureSignalCallbackInstalled();
    EpaKernel *kernelForPathId(const String &path_id) const;
    std::string workerDebugKey(const String &path_id, uint32_t wid) const;
    bool workerDebugIsEnabled(const String &path_id, uint32_t wid) const;
    void setWorkerDebugEnabled(const String &path_id, uint32_t wid, bool enabled);
    void pushEvent(const String &kind, uint32_t wid, uint32_t code, const ElaraOsDbgEip *at, const String &message);
    void pushLogEvent(const String &message);
    bool runTicks(EpaKernel *kernel, uint32_t tick_count, bool stop_on_breakpoint, uint32_t watchdog_ms, String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool kernelIsIdle(EpaKernel *kernel) const;
    bool hasBreakpointHit(EpaKernel *kernel, uint32_t *out_wid, Breakpoint *out_breakpoint = NULL) const;
    String buildSnapshotJson(EpaKernel *kernel, const String &path_id = String()) const;
    String buildEventsJson(bool clear_after_read);
    String buildBreakpointJson() const;
    bool parseUintField(const String &json, const String &field, uint32_t default_value, uint32_t *out_value) const;
    bool parseBoolField(const String &json, const String &field, bool default_value, bool *out_value) const;
    bool parseStringField(const String &json, const String &field, String &out_value) const;
    bool parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const;
};

}

#endif

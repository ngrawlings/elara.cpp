>>>>>>>>>>main>>>>PREFIX>SERVICE_NAME>SERVICE_NAME_UPPER>HOST_NAME>SHIM_NAME
#ifndef %SERVICE_NAME_UPPER%_H
#define %SERVICE_NAME_UPPER%_H

#include <deque>
#include <vector>
#include <libelaracore/memory/String.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include "%HOST_NAME%.h"
#include "%SHIM_NAME%.h"

typedef struct {
    uint8_t block_type;
    uint16_t block_id;
    uint32_t rel_pc;
} %PREFIX%DbgEip;

typedef void (*%PREFIX%KernelDbgCallback)(void *cb_user, int kind, uint8_t wid, uint32_t code, const %PREFIX%DbgEip *at, const char *msg);

namespace elara {

class %SERVICE_NAME% : public sockets::rpc::json::JsonRPCService {
public:
    %SERVICE_NAME%();
    virtual ~%SERVICE_NAME%();
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

    %HOST_NAME% host;
    std::deque<DebugEvent> events;
    std::vector<Breakpoint> breakpoints;
    uint64_t next_event_id;

    static void onKernelDebug(void *cb_user, int kind, uint8_t wid, uint32_t code, const ::%PREFIX%DbgEip *at, const char *msg);
    void ensureDebugCallbackInstalled();
    void pushEvent(const String &kind, uint32_t wid, uint32_t code, const %PREFIX%DbgEip *at, const String &message);
    void pushLogEvent(const String &message);
    bool runTicks(uint32_t tick_count, bool stop_on_breakpoint, String &stop_reason, uint32_t &ticks_ran, String &error_message);
    bool hasBreakpointHit(uint32_t *out_wid, Breakpoint *out_breakpoint = NULL) const;
    String buildSnapshotJson() const;
    String buildEventsJson(bool clear_after_read);
    String buildBreakpointJson() const;
    bool parseUintField(const String &json, const String &field, uint32_t default_value, uint32_t *out_value) const;
    bool parseBoolField(const String &json, const String &field, bool default_value, bool *out_value) const;
    bool parseStringField(const String &json, const String &field, String &out_value) const;
    bool parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const;
};

}

#endif
<<<<<<<<<<main

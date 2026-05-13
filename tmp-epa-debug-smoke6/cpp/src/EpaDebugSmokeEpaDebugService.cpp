#include "EpaDebugSmokeEpaDebugService.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>

extern "C" {
typedef struct EpaKernel EpaKernel;
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif
int epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);
void epa_kernel_set_debug_callback(EpaKernel *k, EpaDebugSmokeKernelDbgCallback cb, void *cb_user);
}

namespace elara {
using sockets::rpc::json::JsonRPCCodec;

namespace {
    static String jsonQuote(const String &value) {
        return String("\"") + JsonRPCCodec::escapeJsonString(value) + String("\"");
    }
    static bool startsWith(const String &value, const char *prefix) {
        String cmp(prefix);
        return value.substr(0, cmp.length()) == cmp;
    }
    class StdoutCapture {
    public:
        StdoutCapture() : active(false), saved_fd(-1) { pipe_fds[0] = -1; pipe_fds[1] = -1; }
        bool begin() {
            fflush(stdout);
            if (pipe(pipe_fds) != 0) return false;
            saved_fd = dup(STDOUT_FILENO);
            if (saved_fd < 0) return false;
            if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) return false;
            active = true;
            return true;
        }
        String end() {
            String result;
            char buffer[256];
            ssize_t read_len;
            if (!active) return result;
            fflush(stdout);
            dup2(saved_fd, STDOUT_FILENO);
            close(saved_fd);
            close(pipe_fds[1]);
            while ((read_len = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
                result += String(buffer, (int)read_len);
            }
            close(pipe_fds[0]);
            active = false;
            return result;
        }
    private:
        bool active;
        int saved_fd;
        int pipe_fds[2];
    };
}

EpaDebugSmokeEpaDebugService::EpaDebugSmokeEpaDebugService() : JsonRPCService("epa"), next_event_id(1) {}

EpaDebugSmokeEpaDebugService::~EpaDebugSmokeEpaDebugService() {}

void EpaDebugSmokeEpaDebugService::onKernelDebug(void *cb_user, int kind, uint8_t wid, uint32_t code, const ::EpaDebugSmokeDbgEip *at, const char *msg) {
    EpaDebugSmokeEpaDebugService *self = (EpaDebugSmokeEpaDebugService*)cb_user;
    if (!self) return;
    const char *label = "event";
    if (kind == 1) label = "break";
    else if (kind == 2) label = "trap";
    else if (kind == 3) label = "exception";
    else if (kind == 4) label = "signal";
    self->pushEvent(String(label), wid, code, at, msg ? String(msg) : String());
}

void EpaDebugSmokeEpaDebugService::ensureDebugCallbackInstalled() {
    if (host.rawKernel()) {
        epa_kernel_set_debug_callback(host.rawKernel(), (EpaDebugSmokeKernelDbgCallback)onKernelDebug, this);
    }
}

void EpaDebugSmokeEpaDebugService::pushEvent(const String &kind, uint32_t wid, uint32_t code, const EpaDebugSmokeDbgEip *at, const String &message) {
    DebugEvent event;
    event.kind = kind;
    event.wid = wid;
    event.code = code;
    event.block_type = at ? at->block_type : 0u;
    event.block_id = at ? at->block_id : 0u;
    event.rel_pc = at ? at->rel_pc : 0u;
    event.message = message;
    events.push_back(event);
    next_event_id++;
}

void EpaDebugSmokeEpaDebugService::pushLogEvent(const String &message) {
    if (!message.length()) return;
    pushEvent(String("log"), 0, 0, NULL, message);
}

bool EpaDebugSmokeEpaDebugService::parseStringField(const String &json, const String &field, String &out_value) const {
    return JsonRPCCodec::getStringField(json, field, out_value);
}

bool EpaDebugSmokeEpaDebugService::parseUintField(const String &json, const String &field, uint32_t default_value, uint32_t *out_value) const {
    String raw;
    String text(json);
    if (JsonRPCCodec::getStringField(text, field, raw)) {
        if (out_value) *out_value = (uint32_t)strtoul(raw.operator char *(), NULL, 10);
        return true;
    }
    String key = String("\"") + field + String("\"");
    int start = text.indexOf(key);
    if (start < 0) { if (out_value) *out_value = default_value; return true; }
    start = text.indexOf(String(":"), start);
    if (start < 0) { if (out_value) *out_value = default_value; return false; }
    start += 1;
    while (start < text.length() && isspace(text.operator char *()[start])) start++;
    int end = start;
    while (end < text.length() && (isdigit(text.operator char *()[end]) || text.operator char *()[end] == 'x' || text.operator char *()[end] == 'X' || (text.operator char *()[end] >= 'a' && text.operator char *()[end] <= 'f') || (text.operator char *()[end] >= 'A' && text.operator char *()[end] <= 'F'))) end++;
    if (end <= start) { if (out_value) *out_value = default_value; return true; }
    raw = text.substr(start, end - start).trim();
    if (out_value) *out_value = (uint32_t)strtoul(raw.operator char *(), NULL, 0);
    return true;
}

bool EpaDebugSmokeEpaDebugService::parseBoolField(const String &json, const String &field, bool default_value, bool *out_value) const {
    String raw;
    String text(json);
    if (JsonRPCCodec::getStringField(text, field, raw)) {
        raw = raw.trim();
        if (out_value) *out_value = (raw == String("1") || raw == String("true") || raw == String("yes") || raw == String("on"));
        return true;
    }
    if (out_value) *out_value = default_value;
    return true;
}

bool EpaDebugSmokeEpaDebugService::parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const {
    String text(hex);
    text = text.trim();
    bytes.clear();
    if (text.startsWith(String("0x")) || text.startsWith(String("0X"))) text = text.substr(2);
    if ((text.length() % 2) != 0) return false;
    for (int i = 0; i < text.length(); i += 2) {
        char chunk[3]; chunk[0] = text.operator char *()[i]; chunk[1] = text.operator char *()[i + 1]; chunk[2] = 0;
        bytes.push_back((unsigned char)strtoul(chunk, NULL, 16));
    }
    return true;
}

bool EpaDebugSmokeEpaDebugService::hasBreakpointHit(uint32_t *out_wid, Breakpoint *out_breakpoint) const {
    EpaKernel *kernel = host.rawKernel();
    size_t i;
    if (!kernel) return false;
    for (i = 0; i < breakpoints.size(); i++) {
        uint32_t wid = 0;
        const Breakpoint &bp = breakpoints[i];
        if (EpaDebugSmoke_epa_debug_any_worker_at(kernel, bp.block_type, bp.block_id, bp.rel_pc, &wid)) {
            if (out_wid) *out_wid = wid;
            if (out_breakpoint) *out_breakpoint = bp;
            return true;
        }
    }
    return false;
}

bool EpaDebugSmokeEpaDebugService::runTicks(uint32_t tick_count, bool stop_on_breakpoint, String &stop_reason, uint32_t &ticks_ran, String &error_message) {
    EpaKernel *kernel = host.rawKernel();
    char err[EPA_MAX_ERR];
    if (!kernel) { error_message = "kernel not created"; return false; }
    ensureDebugCallbackInstalled();
    ticks_ran = 0;
    for (;;) {
        StdoutCapture capture;
        String captured;
        err[0] = 0;
        capture.begin();
        int ok = epa_kernel_run(kernel, 1u, 1, err);
        captured = capture.end();
        if (captured.length()) pushLogEvent(captured.trim());
        ticks_ran++;
        if (!ok) {
            String err_text(err);
            if (!startsWith(err_text, "run: step complete returning to host")) {
                error_message = err_text.length() ? err_text : host.lastError();
                stop_reason = "error";
                return false;
            }
        }
        if (!events.empty()) { stop_reason = events.back().kind; return true; }
        if (stop_on_breakpoint) {
            uint32_t wid = 0; Breakpoint bp;
            if (hasBreakpointHit(&wid, &bp)) {
                EpaDebugSmokeDbgEip at; at.block_type = bp.block_type; at.block_id = bp.block_id; at.rel_pc = bp.rel_pc;
                pushEvent(String("breakpoint"), wid, 0, &at, String("software breakpoint hit"));
                stop_reason = "breakpoint";
                return true;
            }
        }
        if (tick_count != 0 && ticks_ran >= tick_count) { stop_reason = "step"; return true; }
    }
}

String EpaDebugSmokeEpaDebugService::buildSnapshotJson() const {
    String result("{");
    EpaDebugSmokeEpaDebugKernelSnapshot kernel_snapshot;
    EpaDebugSmokeEpaDebugWorkerSnapshot workers[EPADEBUGSMOKE_EPA_DEBUG_MAX_WORKERS];
    size_t worker_count = 0;
    memset(&kernel_snapshot, 0, sizeof(kernel_snapshot));
    memset(workers, 0, sizeof(workers));
    if (host.rawKernel()) {
        EpaDebugSmoke_epa_debug_capture_kernel(host.rawKernel(), &kernel_snapshot);
        worker_count = EpaDebugSmoke_epa_debug_capture_workers(host.rawKernel(), workers, EPADEBUGSMOKE_EPA_DEBUG_MAX_WORKERS);
    }
    result += String("\"kernel\":{");
    result += String("\"prog_loaded\":") + String((int)kernel_snapshot.prog_loaded);
    result += String(",\"rr_cursor\":") + String((int)kernel_snapshot.rr_cursor);
    result += String(",\"current_wid\":") + String((int)kernel_snapshot.current_wid);
    result += String(",\"interrupt_requested\":") + String((int)kernel_snapshot.interrupt_requested);
    result += String(",\"worker_count\":") + String((int)kernel_snapshot.worker_count);
    result += String("},\"workers\":[");
    for (size_t i = 0; i < worker_count; i++) {
        if (i) result += String(",");
        result += String("{");
        result += String("\"wid\":") + String((int)workers[i].wid);
        result += String(",\"halted\":") + String((int)workers[i].halted);
        result += String(",\"blocked\":") + String((int)workers[i].blocked);
        result += String(",\"faulted\":") + String((int)workers[i].faulted);
        result += String(",\"waiting_for_data\":") + String((int)workers[i].waiting_for_data);
        result += String(",\"at_running\":") + String((int)workers[i].at_running);
        result += String(",\"has_current_ghs\":") + String((int)workers[i].has_current_ghs);
        result += String(",\"current_ghs\":") + String((unsigned long long)workers[i].current_ghs);
        result += String(",\"eip\":{\"block_type\":") + String((int)workers[i].eip.block_type) + String(",\"block_id\":") + String((int)workers[i].eip.block_id) + String(",\"rel_pc\":") + String((int)workers[i].eip.rel_pc) + String("}");
        result += String(",\"regs\":[") + String((int)workers[i].csc[0]) + String(",") + String((int)workers[i].csc[1]) + String(",") + String((int)workers[i].csc[2]) + String(",") + String((int)workers[i].csc[3]) + String("]");
        result += String(",\"stack_depth\":") + String((int)workers[i].stack_depth);
        result += String(",\"stack_preview\":[");
        for (uint32_t j = 0; j < workers[i].stack_preview_count; j++) { if (j) result += String(","); result += String((int)workers[i].stack_preview[j]); }
        result += String("]");
        result += String(",\"locals\":[");
        for (uint32_t j = 0; j < EPADEBUGSMOKE_EPA_DEBUG_LOCALS; j++) { if (j) result += String(","); result += String((int)workers[i].locals[j]); }
        result += String("]");
        result += String(",\"local_arena\":{\"top\":") + String((int)workers[i].lbytes_top) + String(",\"cap\":") + String((int)workers[i].lbytes_cap) + String(",\"scope_depth\":") + String((int)workers[i].lscope_depth) + String("}");
        result += String("}");
    }
    result += String("]}");
    return result;
}

String EpaDebugSmokeEpaDebugService::buildEventsJson(bool clear_after_read) {
    String result("{\"events\":[");
    size_t i = 0;
    for (std::deque<DebugEvent>::const_iterator it = events.begin(); it != events.end(); ++it, ++i) {
        if (i) result += String(",");
        result += String("{\"kind\":") + jsonQuote(it->kind);
        result += String(",\"wid\":") + String((int)it->wid);
        result += String(",\"code\":") + String((int)it->code);
        result += String(",\"block_type\":") + String((int)it->block_type);
        result += String(",\"block_id\":") + String((int)it->block_id);
        result += String(",\"rel_pc\":") + String((int)it->rel_pc);
        result += String(",\"message\":") + jsonQuote(it->message) + String("}");
    }
    result += String("]}");
    if (clear_after_read) events.clear();
    return result;
}

String EpaDebugSmokeEpaDebugService::buildBreakpointJson() const {
    String result("{\"breakpoints\":[");
    for (size_t i = 0; i < breakpoints.size(); i++) {
        if (i) result += String(",");
        result += String("{\"block_type\":") + String((int)breakpoints[i].block_type) + String(",\"block_id\":") + String((int)breakpoints[i].block_id) + String(",\"rel_pc\":") + String((int)breakpoints[i].rel_pc) + String("}");
    }
    result += String("]}");
    return result;
}

bool EpaDebugSmokeEpaDebugService::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("ping")) { result_json = String("{\"message\":\"pong\"}"); return true; }
    if (method == String("epa.debug.create")) {
        if (!host.create()) { error_code = String("create_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallbackInstalled(); result_json = String("{\"created\":true}"); return true; }
    if (method == String("epa.debug.destroy")) { host.destroy(); events.clear(); result_json = String("{\"destroyed\":true}"); return true; }
    if (method == String("epa.debug.setKernelId")) { String kernel_id; if (!parseStringField(params_json, String("kernel_id"), kernel_id)) kernel_id = String("epa.debug.kernel"); if (!host.setKernelId(kernel_id)) { error_code = String("set_kernel_id_failed"); error_message = host.lastError(); return false; } result_json = String("{\"ok\":true}"); return true; }
    if (method == String("epa.debug.loadAsm")) { String asm_path; if (!parseStringField(params_json, String("asm_path"), asm_path) || !asm_path.length()) { error_code = String("missing_asm_path"); error_message = String("asm_path is required"); return false; } if (!host.loadAsmPath(asm_path)) { error_code = String("load_asm_failed"); error_message = host.lastError(); return false; } ensureDebugCallbackInstalled(); result_json = String("{\"loaded\":true}"); return true; }
    if (method == String("epa.debug.ingressPushHex")) { uint32_t wid = 1, tag = 0; String payload_hex; std::vector<unsigned char> bytes; parseUintField(params_json, String("wid"), 1, &wid); parseUintField(params_json, String("tag"), 0, &tag); if (!parseStringField(params_json, String("payload_hex"), payload_hex) || !parseHexBytes(payload_hex, bytes)) { error_code = String("invalid_payload_hex"); error_message = String("payload_hex must be an even-length hex string"); return false; } if (!host.ingressPushTagged(wid, tag, bytes.data(), (uint32_t)bytes.size())) { error_code = String("ingress_push_failed"); error_message = host.lastError(); return false; } result_json = String("{\"queued\":true}"); return true; }
    if (method == String("epa.debug.step")) { uint32_t ticks = 1, ticks_ran = 0; String stop_reason; parseUintField(params_json, String("ticks"), 1, &ticks); if (!runTicks(ticks, false, stop_reason, ticks_ran, error_message)) { error_code = String("step_failed"); return false; } result_json = String("{\"ticks_ran\":") + String((int)ticks_ran) + String(",\"stop_reason\":") + jsonQuote(stop_reason) + String(",\"snapshot\":") + buildSnapshotJson() + String("}"); return true; }
    if (method == String("epa.debug.run")) { uint32_t max_ticks = 1000, ticks_ran = 0; String stop_reason; parseUintField(params_json, String("max_ticks"), 1000, &max_ticks); if (!runTicks(max_ticks, true, stop_reason, ticks_ran, error_message)) { error_code = String("run_failed"); return false; } result_json = String("{\"ticks_ran\":") + String((int)ticks_ran) + String(",\"stop_reason\":") + jsonQuote(stop_reason) + String(",\"snapshot\":") + buildSnapshotJson() + String("}"); return true; }
    if (method == String("epa.debug.interrupt")) { if (host.rawKernel()) epa_kernel_request_interrupt(host.rawKernel()); result_json = String("{\"interrupt_requested\":true}"); return true; }
    if (method == String("epa.debug.snapshot")) { result_json = buildSnapshotJson(); return true; }
    if (method == String("epa.debug.events")) { bool clear_after = true; parseBoolField(params_json, String("clear"), true, &clear_after); result_json = buildEventsJson(clear_after); return true; }
    if (method == String("epa.debug.breakpointAdd")) { Breakpoint bp; bp.block_type = 0; bp.block_id = 0; bp.rel_pc = 0; parseUintField(params_json, String("block_type"), 0, (uint32_t*)&bp.block_type); parseUintField(params_json, String("block_id"), 0, (uint32_t*)&bp.block_id); parseUintField(params_json, String("rel_pc"), 0, &bp.rel_pc); breakpoints.push_back(bp); result_json = buildBreakpointJson(); return true; }
    if (method == String("epa.debug.breakpointClear")) { uint32_t block_type = 0, block_id = 0, rel_pc = 0; parseUintField(params_json, String("block_type"), 0, &block_type); parseUintField(params_json, String("block_id"), 0, &block_id); parseUintField(params_json, String("rel_pc"), 0, &rel_pc); for (std::vector<Breakpoint>::iterator it = breakpoints.begin(); it != breakpoints.end();) { if (it->block_type == block_type && it->block_id == block_id && it->rel_pc == rel_pc) it = breakpoints.erase(it); else ++it; } result_json = buildBreakpointJson(); return true; }
    if (method == String("epa.debug.breakpointList")) { result_json = buildBreakpointJson(); return true; }
    error_code = String("unknown_method"); error_message = String("Unsupported EPA debug RPC method"); return false; }

}

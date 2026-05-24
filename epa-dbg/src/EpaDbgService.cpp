#include "EpaDbgService.h"

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
int  epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);
}

namespace elara {
using sockets::rpc::json::JsonRPCCodec;

namespace {

static String jq(const String &v) {
    return String("\"") + JsonRPCCodec::escapeJsonString(v) + String("\"");
}

class StdoutCapture {
public:
    StdoutCapture() : active(false), saved_fd(-1) { fds[0] = fds[1] = -1; }
    bool begin() {
        fflush(stdout);
        if (pipe(fds) != 0) return false;
        saved_fd = dup(STDOUT_FILENO);
        if (saved_fd < 0) return false;
        dup2(fds[1], STDOUT_FILENO);
        active = true;
        return true;
    }
    String end() {
        String result;
        char buf[256];
        ssize_t n;
        if (!active) return result;
        fflush(stdout);
        dup2(saved_fd, STDOUT_FILENO);
        close(saved_fd);
        close(fds[1]);
        while ((n = read(fds[0], buf, sizeof(buf))) > 0)
            result += String(buf, (int)n);
        close(fds[0]);
        active = false;
        return result;
    }
private:
    bool active;
    int  saved_fd;
    int  fds[2];
};

} // anonymous namespace

EpaDbgService::EpaDbgService()
    : JsonRPCService("epa"), next_event_id(1) {}

EpaDbgService::~EpaDbgService() {}

void EpaDbgService::onKernelDebug(void *user, int kind, uint8_t wid, uint32_t code,
                                   const EpaDbgEip *at, const char *msg) {
    EpaDbgService *self = static_cast<EpaDbgService *>(user);
    if (!self) return;
    const char *label = "event";
    if      (kind == 1) label = "break";
    else if (kind == 2) label = "trap";
    else if (kind == 3) label = "exception";
    else if (kind == 4) label = "signal";
    self->pushEvent(String(label), wid, code, at, msg ? String(msg) : String());
}

void EpaDbgService::ensureDebugCallback() {
    EpaKernel *k = host.rawKernel();
    if (k) epa_kernel_set_debug_callback(k, (void *)onKernelDebug, this);
}

void EpaDbgService::pushEvent(const String &kind, uint32_t wid, uint32_t code,
                               const EpaDbgEip *at, const String &message) {
    DebugEvent ev;
    ev.kind       = kind;
    ev.wid        = wid;
    ev.code       = code;
    ev.block_type = at ? at->block_type : 0u;
    ev.block_id   = at ? at->block_id   : 0u;
    ev.rel_pc     = at ? at->rel_pc     : 0u;
    ev.message    = message;
    events.push_back(ev);
    next_event_id++;
}

void EpaDbgService::pushLog(const String &message) {
    if (!message.length()) return;
    pushEvent(String("log"), 0, 0, NULL, message);
}

bool EpaDbgService::parseString(const String &json, const String &field, String &out) const {
    return JsonRPCCodec::getStringField(json, field, out);
}

bool EpaDbgService::parseUint(const String &json, const String &field, uint32_t def, uint32_t *out) const {
    String raw;
    String text(json);
    if (JsonRPCCodec::getStringField(text, field, raw)) {
        if (out) *out = (uint32_t)strtoul(raw.operator char *(), NULL, 10);
        return true;
    }
    String key = String("\"") + field + String("\"");
    int start = text.indexOf(key);
    if (start < 0) { if (out) *out = def; return true; }
    start = text.indexOf(String(":"), start);
    if (start < 0) { if (out) *out = def; return false; }
    start++;
    while (start < text.length() && isspace(text.operator char *()[start])) start++;
    int end = start;
    const char *p = text.operator char *();
    while (end < text.length() && (isxdigit(p[end]) || p[end] == 'x' || p[end] == 'X')) end++;
    if (end <= start) { if (out) *out = def; return true; }
    raw = text.substr(start, end - start).trim();
    if (out) *out = (uint32_t)strtoul(raw.operator char *(), NULL, 0);
    return true;
}

bool EpaDbgService::parseBool(const String &json, const String &field, bool def, bool *out) const {
    String raw;
    String text(json);
    if (JsonRPCCodec::getStringField(text, field, raw)) {
        raw = raw.trim();
        if (out) *out = (raw == String("1") || raw == String("true") || raw == String("yes") || raw == String("on"));
        return true;
    }
    if (out) *out = def;
    return true;
}

bool EpaDbgService::parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const {
    String text(hex);
    text = text.trim();
    bytes.clear();
    if (text.startsWith(String("0x")) || text.startsWith(String("0X"))) text = text.substr(2);
    if ((text.length() % 2) != 0) return false;
    for (int i = 0; i < text.length(); i += 2) {
        char chunk[3];
        chunk[0] = text.operator char *()[i];
        chunk[1] = text.operator char *()[i + 1];
        chunk[2] = 0;
        bytes.push_back((unsigned char)strtoul(chunk, NULL, 16));
    }
    return true;
}

bool EpaDbgService::hasBreakpointHit(uint32_t *out_wid, Breakpoint *out_bp) const {
    EpaKernel *k = host.rawKernel();
    if (!k) return false;
    for (size_t i = 0; i < breakpoints.size(); i++) {
        uint32_t wid = 0;
        const Breakpoint &bp = breakpoints[i];
        if (epa_dbg_any_worker_at(k, bp.block_type, bp.block_id, bp.rel_pc, &wid)) {
            if (out_wid) *out_wid = wid;
            if (out_bp)  *out_bp  = bp;
            return true;
        }
    }
    return false;
}

bool EpaDbgService::runTicks(uint32_t tick_count, bool stop_on_bp,
                              String &stop_reason, uint32_t &ticks_ran, String &error_message) {
    EpaKernel *k = host.rawKernel();
    char err[EPA_MAX_ERR];
    if (!k) { error_message = String("kernel not created"); return false; }
    ensureDebugCallback();
    ticks_ran = 0;
    for (;;) {
        StdoutCapture cap;
        String captured;
        err[0] = 0;
        cap.begin();
        int ok = epa_kernel_run(k, 1u, 1, err);
        captured = cap.end();
        if (captured.length()) pushLog(captured.trim());
        ticks_ran++;
        if (!ok) {
            String et(err);
            if (et.substr(0, 36) != String("run: step complete returning to host")) {
                error_message = et.length() ? et : host.lastError();
                stop_reason   = String("error");
                return false;
            }
        }
        if (!events.empty()) { stop_reason = events.back().kind; return true; }
        if (stop_on_bp) {
            uint32_t wid = 0; Breakpoint bp;
            if (hasBreakpointHit(&wid, &bp)) {
                EpaDbgEip at; at.block_type = bp.block_type; at.block_id = bp.block_id; at.rel_pc = bp.rel_pc;
                pushEvent(String("breakpoint"), wid, 0, &at, String("breakpoint hit"));
                stop_reason = String("breakpoint");
                return true;
            }
        }
        if (tick_count != 0 && ticks_ran >= tick_count) { stop_reason = String("step"); return true; }
    }
}

bool EpaDbgService::runToWait(uint32_t target_wid, uint32_t max_ticks,
                               String &stop_reason, uint32_t &ticks_ran, String &error_message) {
    EpaKernel *k = host.rawKernel();
    char err[EPA_MAX_ERR];
    if (!k) { error_message = String("kernel not created"); return false; }
    ensureDebugCallback();
    ticks_ran = 0;
    for (;;) {
        StdoutCapture cap;
        err[0] = 0;
        cap.begin();
        int ok = epa_kernel_run(k, 1u, 1, err);
        String captured = cap.end();
        if (captured.length()) pushLog(captured.trim());
        ticks_ran++;
        if (!ok) {
            String et(err);
            if (et.substr(0, 36) != String("run: step complete returning to host")) {
                error_message = et.length() ? et : host.lastError();
                stop_reason = String("error");
                return false;
            }
        }
        EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
        size_t wc = epa_dbg_capture_workers(k, ws, EPA_DBG_MAX_WORKERS);
        for (size_t i = 0; i < wc; i++) {
            if (ws[i].wid == target_wid) {
                if (ws[i].waiting_for_data) { stop_reason = String("wait_for_data"); return true; }
                if (ws[i].halted)           { stop_reason = String("halted");         return true; }
                if (ws[i].faulted)          { stop_reason = String("faulted");        return true; }
                break;
            }
        }
        if (!events.empty()) { stop_reason = events.back().kind; return true; }
        if (max_ticks != 0 && ticks_ran >= max_ticks) { stop_reason = String("max_ticks"); return true; }
    }
}

String EpaDbgService::buildSnapshotJson() const {
    String result("{");
    EpaDbgKernelSnapshot ks;
    EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
    size_t wc = 0;
    memset(&ks, 0, sizeof(ks));
    memset(ws, 0, sizeof(ws));
    if (host.rawKernel()) {
        epa_dbg_capture_kernel(host.rawKernel(), &ks);
        wc = epa_dbg_capture_workers(host.rawKernel(), ws, EPA_DBG_MAX_WORKERS);
    }
    result += String("\"kernel\":{");
    result += String("\"prog_loaded\":") + String((int)ks.prog_loaded);
    result += String(",\"rr_cursor\":") + String((int)ks.rr_cursor);
    result += String(",\"current_wid\":") + String((int)ks.current_wid);
    result += String(",\"interrupt_requested\":") + String((int)ks.interrupt_requested);
    result += String(",\"worker_count\":") + String((int)ks.worker_count);
    result += String("},\"workers\":[");
    for (size_t i = 0; i < wc; i++) {
        if (i) result += String(",");
        const EpaDbgWorkerSnapshot &w = ws[i];
        result += String("{");
        result += String("\"wid\":") + String((int)w.wid);
        result += String(",\"halted\":") + String((int)w.halted);
        result += String(",\"blocked\":") + String((int)w.blocked);
        result += String(",\"faulted\":") + String((int)w.faulted);
        result += String(",\"waiting_for_data\":") + String((int)w.waiting_for_data);
        result += String(",\"at_running\":") + String((int)w.at_running);
        result += String(",\"has_current_ghs\":") + String((int)w.has_current_ghs);
        result += String(",\"current_ghs\":") + String((unsigned long long)w.current_ghs);
        result += String(",\"eip\":{\"block_type\":") + String((int)w.eip.block_type)
               + String(",\"block_id\":") + String((int)w.eip.block_id)
               + String(",\"rel_pc\":") + String((int)w.eip.rel_pc) + String("}");
        result += String(",\"regs\":[") + String((int)w.csc[0]) + String(",") + String((int)w.csc[1])
               + String(",") + String((int)w.csc[2]) + String(",") + String((int)w.csc[3]) + String("]");
        result += String(",\"inq_count\":") + String((int)w.inq_count);
        result += String(",\"outq_count\":") + String((int)w.outq_count);
        result += String(",\"stack_depth\":") + String((int)w.stack_depth);
        result += String(",\"stack_preview\":[");
        for (uint32_t j = 0; j < w.stack_preview_count; j++) {
            if (j) result += String(",");
            result += String((int)w.stack_preview[j]);
        }
        result += String("]");
        result += String(",\"locals\":[");
        for (uint32_t j = 0; j < EPA_DBG_LOCALS; j++) {
            if (j) result += String(",");
            result += String((int)w.locals[j]);
        }
        result += String("]");
        result += String(",\"local_arena\":{\"top\":") + String((int)w.lbytes_top)
               + String(",\"cap\":") + String((int)w.lbytes_cap)
               + String(",\"scope_depth\":") + String((int)w.lscope_depth) + String("}");
        result += String("}");
    }
    result += String("]}");
    return result;
}

String EpaDbgService::buildEventsJson(bool clear_after_read) {
    String result("{\"events\":[");
    size_t i = 0;
    for (std::deque<DebugEvent>::const_iterator it = events.begin(); it != events.end(); ++it, ++i) {
        if (i) result += String(",");
        result += String("{\"kind\":") + jq(it->kind);
        result += String(",\"wid\":") + String((int)it->wid);
        result += String(",\"code\":") + String((int)it->code);
        result += String(",\"block_type\":") + String((int)it->block_type);
        result += String(",\"block_id\":") + String((int)it->block_id);
        result += String(",\"rel_pc\":") + String((int)it->rel_pc);
        result += String(",\"message\":") + jq(it->message) + String("}");
    }
    result += String("]}");
    if (clear_after_read) events.clear();
    return result;
}

String EpaDbgService::buildBreakpointJson() const {
    String result("{\"breakpoints\":[");
    for (size_t i = 0; i < breakpoints.size(); i++) {
        if (i) result += String(",");
        result += String("{\"block_type\":") + String((int)breakpoints[i].block_type)
               + String(",\"block_id\":") + String((int)breakpoints[i].block_id)
               + String(",\"rel_pc\":") + String((int)breakpoints[i].rel_pc) + String("}");
    }
    result += String("]}");
    return result;
}

bool EpaDbgService::call(const String &method, const String &params_json,
                          String &result_json, String &error_code, String &error_message) {

    if (method == String("ping")) {
        result_json = String("{\"message\":\"pong\"}"); return true;
    }
    if (method == String("epa.debug.create")) {
        if (!host.create()) { error_code = String("create_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallback(); result_json = String("{\"created\":true}"); return true;
    }
    if (method == String("epa.debug.destroy")) {
        host.destroy(); events.clear(); result_json = String("{\"destroyed\":true}"); return true;
    }
    if (method == String("epa.debug.setKernelId")) {
        String id;
        if (!parseString(params_json, String("kernel_id"), id)) id = String("epa.debug.kernel");
        if (!host.setKernelId(id)) { error_code = String("set_kernel_id_failed"); error_message = host.lastError(); return false; }
        result_json = String("{\"ok\":true}"); return true;
    }
    if (method == String("epa.debug.loadAsm")) {
        String path;
        if (!parseString(params_json, String("asm_path"), path) || !path.length()) {
            error_code = String("missing_asm_path"); error_message = String("asm_path is required"); return false;
        }
        if (!host.loadAsmPath(path)) { error_code = String("load_asm_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallback(); result_json = String("{\"loaded\":true}"); return true;
    }
    if (method == String("epa.debug.loadBundle")) {
        String path;
        if (!parseString(params_json, String("bundle_path"), path) || !path.length()) {
            error_code = String("missing_bundle_path"); error_message = String("bundle_path is required"); return false;
        }
        if (!host.loadBundlePath(path)) { error_code = String("load_bundle_failed"); error_message = host.lastError(); return false; }
        result_json = String("{\"loaded\":true}"); return true;
    }
    if (method == String("epa.debug.ingressPushHex")) {
        uint32_t wid = 1, tag = 0;
        String hex;
        std::vector<unsigned char> bytes;
        parseUint(params_json, String("wid"), 1, &wid);
        parseUint(params_json, String("tag"), 0, &tag);
        if (!parseString(params_json, String("payload_hex"), hex) || !parseHexBytes(hex, bytes)) {
            error_code = String("invalid_payload_hex"); error_message = String("payload_hex must be an even-length hex string"); return false;
        }
        if (!host.ingressPushTagged(wid, tag, bytes.data(), (uint32_t)bytes.size())) {
            error_code = String("ingress_push_failed"); error_message = host.lastError(); return false;
        }
        result_json = String("{\"queued\":true}"); return true;
    }
    if (method == String("epa.debug.step")) {
        uint32_t ticks = 1, ticks_ran = 0;
        String stop_reason;
        parseUint(params_json, String("ticks"), 1, &ticks);
        if (!runTicks(ticks, false, stop_reason, ticks_ran, error_message)) {
            error_code = String("step_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson() + String("}");
        return true;
    }
    if (method == String("epa.debug.run")) {
        uint32_t max_ticks = 1000, ticks_ran = 0;
        String stop_reason;
        parseUint(params_json, String("max_ticks"), 1000, &max_ticks);
        if (!runTicks(max_ticks, true, stop_reason, ticks_ran, error_message)) {
            error_code = String("run_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson() + String("}");
        return true;
    }
    if (method == String("epa.debug.runToWait")) {
        uint32_t wid = 0, max_ticks = 500000, ticks_ran = 0;
        String stop_reason;
        parseUint(params_json, String("wid"),       0,      &wid);
        parseUint(params_json, String("max_ticks"), 500000, &max_ticks);
        if (!runToWait(wid, max_ticks, stop_reason, ticks_ran, error_message)) {
            error_code = String("run_to_wait_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson() + String("}");
        return true;
    }
    if (method == String("epa.debug.interrupt")) {
        EpaKernel *k = host.rawKernel();
        if (k) epa_kernel_request_interrupt(k);
        result_json = String("{\"interrupt_requested\":true}"); return true;
    }
    if (method == String("epa.debug.snapshot")) {
        result_json = buildSnapshotJson(); return true;
    }
    if (method == String("epa.debug.events")) {
        bool clear = true;
        parseBool(params_json, String("clear"), true, &clear);
        result_json = buildEventsJson(clear); return true;
    }
    if (method == String("epa.debug.breakpointAdd")) {
        Breakpoint bp; bp.block_type = 0; bp.block_id = 0; bp.rel_pc = 0;
        parseUint(params_json, String("block_type"), 0, (uint32_t *)&bp.block_type);
        parseUint(params_json, String("block_id"),   0, (uint32_t *)&bp.block_id);
        parseUint(params_json, String("rel_pc"),     0, &bp.rel_pc);
        breakpoints.push_back(bp);
        result_json = buildBreakpointJson(); return true;
    }
    if (method == String("epa.debug.breakpointClear")) {
        uint32_t bt = 0, bi = 0, rpc = 0;
        parseUint(params_json, String("block_type"), 0, &bt);
        parseUint(params_json, String("block_id"),   0, &bi);
        parseUint(params_json, String("rel_pc"),     0, &rpc);
        for (std::vector<Breakpoint>::iterator it = breakpoints.begin(); it != breakpoints.end(); ) {
            if (it->block_type == bt && it->block_id == bi && it->rel_pc == rpc)
                it = breakpoints.erase(it);
            else ++it;
        }
        result_json = buildBreakpointJson(); return true;
    }
    if (method == String("epa.debug.breakpointList")) {
        result_json = buildBreakpointJson(); return true;
    }

    error_code    = String("unknown_method");
    error_message = String("Unknown EPA debug RPC method: ") + method;
    return false;
}

} // namespace elara

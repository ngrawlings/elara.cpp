#include "OrangeFortressEpaDebugService.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
#include "OrangeFortressEpaFrame.h"

extern "C" {
typedef struct EpaKernel EpaKernel;
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif
int  epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);
void epa_kernel_set_debug_callback(EpaKernel *k, OrangeFortressKernelDbgCallback cb, void *cb_user);
void epa_kernel_set_signal_callback(EpaKernel *k, int (*cb)(uint8_t wid, const char *msg, const int msg_len));
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

static OrangeFortressEpaDebugService *g_debug_service = NULL;

OrangeFortressEpaDebugService::OrangeFortressEpaDebugService()
    : JsonRPCService("epa"), next_event_id(1), last_mailbox_wid(0) {}

OrangeFortressEpaDebugService::~OrangeFortressEpaDebugService() {}

void OrangeFortressEpaDebugService::onKernelDebug(void *cb_user, int kind, uint8_t wid, uint32_t code, const ::OrangeFortressDbgEip *at, const char *msg) {
    OrangeFortressEpaDebugService *self = (OrangeFortressEpaDebugService*)cb_user;
    if (!self) return;
    const char *label = "event";
    if (kind == 1) label = "break";
    else if (kind == 2) label = "trap";
    else if (kind == 3) label = "exception";
    else if (kind == 4) label = "signal";
    self->pushEvent(String(label), wid, code, at, msg ? String(msg) : String());
}

int OrangeFortressEpaDebugService::onSignalMailbox(uint8_t wid, const char *data, const int len) {
    if (g_debug_service && data && len > 0) {
        g_debug_service->last_mailbox_wid = wid;
        g_debug_service->last_mailbox_bytes.assign(
            (const uint8_t *)data, (const uint8_t *)data + (size_t)len);
    }
    return 1;
}

void OrangeFortressEpaDebugService::ensureDebugCallbackInstalled() {
    if (host.rawKernel()) {
        epa_kernel_set_debug_callback(host.rawKernel(), (OrangeFortressKernelDbgCallback)onKernelDebug, this);
    }
    size_t count = host.kernelCount();
    for (size_t i = 0; i < count; i++) {
        EpaKernel *k = host.rawKernelAt(i);
        if (k) epa_kernel_set_debug_callback(k, (OrangeFortressKernelDbgCallback)onKernelDebug, this);
    }
}

void OrangeFortressEpaDebugService::ensureSignalCallbackInstalled() {
    g_debug_service = this;
    if (host.rawKernel()) {
        epa_kernel_set_signal_callback(host.rawKernel(), onSignalMailbox);
    }
    size_t count = host.kernelCount();
    for (size_t i = 0; i < count; i++) {
        EpaKernel *k = host.rawKernelAt(i);
        if (k) epa_kernel_set_signal_callback(k, onSignalMailbox);
    }
}

EpaKernel *OrangeFortressEpaDebugService::kernelForPathId(const String &path_id) const {
    if (!path_id.length()) {
        return host.rawKernel();
    }
    int idx = host.findKernelIndex(path_id);
    if (idx >= 0) return host.rawKernelAt((size_t)idx);
    return NULL;
}

std::string OrangeFortressEpaDebugService::workerDebugKey(const String &path_id, uint32_t wid) const {
    String pid(path_id);
    return std::string(pid.operator char *()) + ":" + std::to_string((unsigned int)wid);
}

bool OrangeFortressEpaDebugService::workerDebugIsEnabled(const String &path_id, uint32_t wid) const {
    std::map<std::string, bool>::const_iterator it = worker_debug_enabled.find(workerDebugKey(path_id, wid));
    if (it == worker_debug_enabled.end()) return true;
    return it->second;
}

void OrangeFortressEpaDebugService::setWorkerDebugEnabled(const String &path_id, uint32_t wid, bool enabled) {
    worker_debug_enabled[workerDebugKey(path_id, wid)] = enabled;
}

void OrangeFortressEpaDebugService::pushEvent(const String &kind, uint32_t wid, uint32_t code, const OrangeFortressDbgEip *at, const String &message) {
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

void OrangeFortressEpaDebugService::pushLogEvent(const String &message) {
    if (!message.length()) return;
    String text(message);
    if (!startsWith(text, "[epa vm]")) {
        text = String("[epa vm] ") + text;
    }
    printf("%s\n", text.operator char *());
    fflush(stdout);
    pushEvent(String("log"), 0, 0, NULL, text);
}

bool OrangeFortressEpaDebugService::parseStringField(const String &json, const String &field, String &out_value) const {
    return JsonRPCCodec::getStringField(json, field, out_value);
}

bool OrangeFortressEpaDebugService::parseUintField(const String &json, const String &field, uint32_t default_value, uint32_t *out_value) const {
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

bool OrangeFortressEpaDebugService::parseBoolField(const String &json, const String &field, bool default_value, bool *out_value) const {
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

bool OrangeFortressEpaDebugService::parseHexBytes(const String &hex, std::vector<unsigned char> &bytes) const {
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

bool OrangeFortressEpaDebugService::hasBreakpointHit(EpaKernel *kernel, uint32_t *out_wid, Breakpoint *out_breakpoint) const {
    size_t i;
    if (!kernel) return false;
    for (i = 0; i < breakpoints.size(); i++) {
        uint32_t wid = 0;
        const Breakpoint &bp = breakpoints[i];
        if (OrangeFortress_epa_debug_any_worker_at(kernel, bp.block_type, bp.block_id, bp.rel_pc, &wid)) {
            if (out_wid) *out_wid = wid;
            if (out_breakpoint) *out_breakpoint = bp;
            return true;
        }
    }
    return false;
}

bool OrangeFortressEpaDebugService::runTicks(EpaKernel *kernel, uint32_t tick_count, bool stop_on_breakpoint, String &stop_reason, uint32_t &ticks_ran, String &error_message) {
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
            if (hasBreakpointHit(kernel, &wid, &bp)) {
                OrangeFortressDbgEip at; at.block_type = bp.block_type; at.block_id = bp.block_id; at.rel_pc = bp.rel_pc;
                pushEvent(String("breakpoint"), wid, 0, &at, String("software breakpoint hit"));
                stop_reason = "breakpoint";
                return true;
            }
        }
        if (tick_count != 0 && ticks_ran >= tick_count) { stop_reason = "step"; return true; }
    }
}

String OrangeFortressEpaDebugService::buildSnapshotJson(EpaKernel *kernel, const String &path_id) const {
    String kernel_snapshot_path_id(path_id);
    String result("{");
    OrangeFortressEpaDebugKernelSnapshot kernel_snapshot;
    OrangeFortressEpaDebugWorkerSnapshot workers[ORANGEFORTRESS_EPA_DEBUG_MAX_WORKERS];
    size_t worker_count = 0;
    memset(&kernel_snapshot, 0, sizeof(kernel_snapshot));
    memset(workers, 0, sizeof(workers));
    if (kernel) {
        OrangeFortress_epa_debug_capture_kernel(kernel, &kernel_snapshot);
        worker_count = OrangeFortress_epa_debug_capture_workers(kernel, workers, ORANGEFORTRESS_EPA_DEBUG_MAX_WORKERS);
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
        for (uint32_t j = 0; j < ORANGEFORTRESS_EPA_DEBUG_LOCALS; j++) { if (j) result += String(","); result += String((int)workers[i].locals[j]); }
        result += String("]");
        result += String(",\"local_arena\":{\"top\":") + String((int)workers[i].lbytes_top) + String(",\"cap\":") + String((int)workers[i].lbytes_cap) + String(",\"scope_depth\":") + String((int)workers[i].lscope_depth) + String("}");
        result += String(",\"debug_enabled\":") + String(workerDebugIsEnabled(kernel_snapshot_path_id, workers[i].wid) ? "true" : "false");
        result += String("}");
    }
    result += String("]}");
    return result;
}

String OrangeFortressEpaDebugService::buildEventsJson(bool clear_after_read) {
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

String OrangeFortressEpaDebugService::buildBreakpointJson() const {
    String result("{\"breakpoints\":[");
    for (size_t i = 0; i < breakpoints.size(); i++) {
        if (i) result += String(",");
        result += String("{\"block_type\":") + String((int)breakpoints[i].block_type) + String(",\"block_id\":") + String((int)breakpoints[i].block_id) + String(",\"rel_pc\":") + String((int)breakpoints[i].rel_pc) + String("}");
    }
    result += String("]}");
    return result;
}

bool OrangeFortressEpaDebugService::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("ping")) { result_json = String("{\"message\":\"pong\"}"); return true; }
    if (method == String("epa.debug.create")) {
        if (!host.create()) { error_code = String("create_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallbackInstalled(); result_json = String("{\"created\":true}"); return true; }
    if (method == String("epa.debug.destroy")) { host.destroy(); events.clear(); last_mailbox_bytes.clear(); worker_debug_enabled.clear(); result_json = String("{\"destroyed\":true}"); return true; }
    if (method == String("epa.debug.setKernelId")) { String kernel_id; if (!parseStringField(params_json, String("kernel_id"), kernel_id)) kernel_id = String("epa.debug.kernel"); if (!host.setKernelId(kernel_id)) { error_code = String("set_kernel_id_failed"); error_message = host.lastError(); return false; } result_json = String("{\"ok\":true}"); return true; }
    if (method == String("epa.debug.loadAsm")) { String asm_path; if (!parseStringField(params_json, String("asm_path"), asm_path) || !asm_path.length()) { error_code = String("missing_asm_path"); error_message = String("asm_path is required"); return false; } if (!host.loadAsmPath(asm_path)) { error_code = String("load_asm_failed"); error_message = host.lastError(); return false; } ensureDebugCallbackInstalled(); ensureSignalCallbackInstalled(); result_json = String("{\"loaded\":true}"); return true; }
    if (method == String("epa.debug.loadBundle")) {
        String bundle_path;
        if (!parseStringField(params_json, String("bundle_path"), bundle_path) || !bundle_path.length()) {
            error_code = String("missing_bundle_path"); error_message = String("bundle_path is required"); return false;
        }
        if (!host.loadBundlePath(bundle_path)) { error_code = String("load_bundle_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallbackInstalled();
        ensureSignalCallbackInstalled();
        result_json = String("{\"loaded\":true,\"kernel_count\":") + String((int)host.kernelCount()) + String("}");
        return true;
    }
    if (method == String("epa.debug.ingressPushHex")) {
        uint32_t wid = 1, tag = 0; String payload_hex, path_id; std::vector<unsigned char> bytes;
        parseStringField(params_json, String("path_id"), path_id);
        parseUintField(params_json, String("wid"), 1, &wid);
        parseUintField(params_json, String("tag"), 0, &tag);
        if (!parseStringField(params_json, String("payload_hex"), payload_hex) || !parseHexBytes(payload_hex, bytes)) {
            error_code = String("invalid_payload_hex"); error_message = String("payload_hex must be an even-length hex string"); return false;
        }
        EpaKernel *target = kernelForPathId(path_id);
        if (!target) { error_code = String("kernel_not_found"); error_message = String("no kernel matches path_id"); return false; }
        int idx = path_id.length() ? host.findKernelIndex(path_id) : -1;
        bool ok = (idx >= 0)
            ? host.ingressPushTaggedToKernel((size_t)idx, wid, tag, bytes.data(), (uint32_t)bytes.size())
            : host.ingressPushTagged(wid, tag, bytes.data(), (uint32_t)bytes.size());
        if (!ok) { error_code = String("ingress_push_failed"); error_message = host.lastError(); return false; }
        result_json = String("{\"queued\":true")
                    + String(",\"path_id\":") + jsonQuote(path_id)
                    + String(",\"kernel_index\":") + String(idx)
                    + String(",\"wid\":") + String((int)wid)
                    + String(",\"tag\":") + String((int)tag)
                    + String(",\"payload_bytes\":") + String((int)bytes.size())
                    + String("}");
        return true;
    }
    if (method == String("epa.debug.step")) {
        String path_id; uint32_t ticks = 1, ticks_ran = 0; String stop_reason;
        parseStringField(params_json, String("path_id"), path_id);
        parseUintField(params_json, String("ticks"), 1, &ticks);
        EpaKernel *kernel = kernelForPathId(path_id);
        if (!runTicks(kernel, ticks, false, stop_reason, ticks_ran, error_message)) { error_code = String("step_failed"); return false; }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran) + String(",\"stop_reason\":") + jsonQuote(stop_reason) + String(",\"snapshot\":") + buildSnapshotJson(kernel, path_id) + String("}"); return true;
    }
    if (method == String("epa.debug.run")) {
        String path_id; uint32_t max_ticks = 1000, ticks_ran = 0; String stop_reason;
        parseStringField(params_json, String("path_id"), path_id);
        parseUintField(params_json, String("max_ticks"), 1000, &max_ticks);
        EpaKernel *kernel = kernelForPathId(path_id);
        if (!runTicks(kernel, max_ticks, true, stop_reason, ticks_ran, error_message)) { error_code = String("run_failed"); return false; }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran) + String(",\"stop_reason\":") + jsonQuote(stop_reason) + String(",\"snapshot\":") + buildSnapshotJson(kernel, path_id) + String("}"); return true;
    }
    if (method == String("epa.debug.interrupt")) {
        String path_id; parseStringField(params_json, String("path_id"), path_id);
        EpaKernel *kernel = kernelForPathId(path_id);
        if (kernel) epa_kernel_request_interrupt(kernel);
        result_json = String("{\"interrupt_requested\":true}"); return true;
    }
    if (method == String("epa.debug.snapshot")) {
        String path_id; parseStringField(params_json, String("path_id"), path_id);
        result_json = buildSnapshotJson(kernelForPathId(path_id), path_id); return true;
    }
    if (method == String("epa.debug.getMailbox")) {
        String hex;
        char tmp[3];
        for (size_t i = 0; i < last_mailbox_bytes.size(); i++) {
            snprintf(tmp, sizeof(tmp), "%02x", (unsigned)last_mailbox_bytes[i]);
            hex += String(tmp);
        }
        OrangeFortressEpaFrameHeader frame_header = orangeFortressParseEgressFrameHeader(
            last_mailbox_bytes.empty() ? NULL : &last_mailbox_bytes[0],
            last_mailbox_bytes.size()
        );
        result_json = String("{\"wid\":") + String((int)last_mailbox_wid)
                    + String(",\"len\":") + String((int)last_mailbox_bytes.size())
                    + String(",\"frame\":") + orangeFortressFrameHeaderJson(
                        frame_header,
                        String("egress"),
                        String("orange-fortress.epa.frame.v1")
                    )
                    + String(",\"hex\":\"") + hex + String("\"}");
        return true;
    }
    if (method == String("epa.debug.clearMailbox")) {
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        result_json = String("{\"cleared\":true}");
        return true;
    }
    if (method == String("epa.debug.events")) { bool clear_after = true; parseBoolField(params_json, String("clear"), true, &clear_after); result_json = buildEventsJson(clear_after); return true; }
    if (method == String("epa.debug.breakpointAdd")) { Breakpoint bp; bp.block_type = 0; bp.block_id = 0; bp.rel_pc = 0; parseUintField(params_json, String("block_type"), 0, (uint32_t*)&bp.block_type); parseUintField(params_json, String("block_id"), 0, (uint32_t*)&bp.block_id); parseUintField(params_json, String("rel_pc"), 0, &bp.rel_pc); breakpoints.push_back(bp); result_json = buildBreakpointJson(); return true; }
    if (method == String("epa.debug.breakpointClear")) { uint32_t block_type = 0, block_id = 0, rel_pc = 0; parseUintField(params_json, String("block_type"), 0, &block_type); parseUintField(params_json, String("block_id"), 0, &block_id); parseUintField(params_json, String("rel_pc"), 0, &rel_pc); for (std::vector<Breakpoint>::iterator it = breakpoints.begin(); it != breakpoints.end();) { if (it->block_type == block_type && it->block_id == block_id && it->rel_pc == rel_pc) it = breakpoints.erase(it); else ++it; } result_json = buildBreakpointJson(); return true; }
    if (method == String("epa.debug.breakpointList")) { result_json = buildBreakpointJson(); return true; }
    if (method == String("epa.debug.setWorkerDebug")) {
        uint32_t wid = 1; bool enabled = true; String path_id;
        parseUintField(params_json, String("wid"), 1, &wid);
        parseBoolField(params_json, String("enabled"), true, &enabled);
        parseStringField(params_json, String("path_id"), path_id);
        setWorkerDebugEnabled(path_id, wid, enabled);
        result_json = String("{\"wid\":") + String((int)wid)
                    + String(",\"debug_enabled\":") + String(enabled ? "true" : "false")
                    + String(",\"snapshot\":") + buildSnapshotJson(kernelForPathId(path_id), path_id)
                    + String("}");
        return true;
    }
    if (method == String("epa.debug.getWorkerDebug")) {
        uint32_t wid = 1; String path_id;
        parseUintField(params_json, String("wid"), 1, &wid);
        parseStringField(params_json, String("path_id"), path_id);
        bool enabled = workerDebugIsEnabled(path_id, wid);
        result_json = String("{\"wid\":") + String((int)wid)
                    + String(",\"debug_enabled\":") + String(enabled ? "true" : "false")
                    + String("}");
        return true;
    }
    error_code = String("unknown_method"); error_message = String("Unsupported EPA debug RPC method"); return false; }

}

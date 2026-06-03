#include "EpaDbgService.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
#include "opcodes/epa_opcode_values.h"

extern "C" {
typedef struct EpaKernel EpaKernel;
void epa_kernel_set_signal_callback(EpaKernel *k, int (*cb)(uint8_t wid, const char *msg, const int msg_len));
}

namespace elara {
using sockets::rpc::json::JsonRPCCodec;

namespace {

static String jq(const String &v) {
    return String("\"") + JsonRPCCodec::escapeJsonString(v) + String("\"");
}

static bool startsWith(const String &value, const char *prefix) {
    String cmp(prefix);
    return value.substr(0, cmp.length()) == cmp;
}

static const uint32_t EPA_DBG_OVERLAY_BREAK_CODE = 0xEADB0001u;
static const size_t EPA_DBG_BREAK_OPCODE_LEN = 6u;

static void writeLe16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void writeLe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static String hexBytes(const uint8_t *bytes, size_t count) {
    static const char *HEX = "0123456789ABCDEF";
    String out;
    for (size_t i = 0; i < count; i++) {
        unsigned char b = bytes[i];
        char pair[3];
        pair[0] = HEX[(b >> 4) & 0xF];
        pair[1] = HEX[b & 0xF];
        pair[2] = 0;
        out += String(pair);
    }
    return out;
}

static const char *ghsTypeName(uint32_t type) {
    switch (type) {
        case 1: return "bytes";
        case 2: return "i32";
        case 3: return "f32";
        case 4: return "pixels";
        case 5: return "string";
        default: return "none";
    }
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

static EpaDbgService *g_debug_service = NULL;

} // anonymous namespace

EpaDbgService::EpaDbgService()
    : JsonRPCService("epa"), last_mailbox_wid(0), next_event_id(1) {}

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
    else if (kind == 5) label = "egress";
    self->pushEvent(String(label), wid, code, at, msg ? String(msg) : String());
}

int EpaDbgService::onSignalMailbox(uint8_t wid, const char *data, const int len) {
    if (g_debug_service && data && len > 0) {
        g_debug_service->last_mailbox_wid = wid;
        g_debug_service->last_mailbox_bytes.assign(
            (const uint8_t *)data, (const uint8_t *)data + (size_t)len);
    }
    return 1;
}

EpaKernel *EpaDbgService::activeKernel() const {
    EpaKernel *k = host.rawKernel();
    if (k) return k;
    size_t n = host.kernelCount();
    for (size_t i = 0; i < n; i++) {
        EpaKernel *mk = host.rawKernelAt(i);
        if (mk) return mk;
    }
    return NULL;
}

EpaKernel *EpaDbgService::kernelForPath(const String &path_id) const {
    if (!path_id.length()) return activeKernel();
    if (host.rawKernel()) return host.rawKernel();
    if (host.kernelCount() > 0) {
        int idx = 0;
        int found = host.findKernelIndex(path_id);
        if (found >= 0) idx = found;
        return host.rawKernelAt((size_t)idx);
    }
    return NULL;
}

void EpaDbgService::ensureDebugCallback() {
    // Set callback on single kernel or every module kernel.
    EpaKernel *k = host.rawKernel();
    if (k) {
        epa_kernel_set_debug_callback(k, (void *)onKernelDebug, this);
        return;
    }
    size_t n = host.kernelCount();
    for (size_t i = 0; i < n; i++) {
        EpaKernel *mk = host.rawKernelAt(i);
        if (mk) epa_kernel_set_debug_callback(mk, (void *)onKernelDebug, this);
    }
}

void EpaDbgService::ensureSignalCallback() {
    g_debug_service = this;
    if (host.rawKernel()) {
        epa_kernel_set_signal_callback(host.rawKernel(), onSignalMailbox);
    }
    size_t n = host.kernelCount();
    for (size_t i = 0; i < n; i++) {
        EpaKernel *mk = host.rawKernelAt(i);
        if (mk) epa_kernel_set_signal_callback(mk, onSignalMailbox);
    }
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
    if (JsonRPCCodec::getStringField(json, field, out)) return true;
    String text(json);
    String key = String("\"") + field + String("\"");
    int start = text.indexOf(key);
    if (start < 0) return false;
    start = text.indexOf(String(":"), start);
    if (start < 0) return false;
    start++;
    while (start < text.length() && isspace(text.operator char *()[start])) start++;
    if (start >= text.length() || text.operator char *()[start] != '"') return false;
    start++;
    String value;
    bool escaped = false;
    const char *p = text.operator char *();
    for (int i = start; i < text.length(); i++) {
        char ch = p[i];
        if (escaped) {
            value += String(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            out = value;
            return true;
        }
        value += String(ch);
    }
    return false;
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
    String key = String("\"") + field + String("\"");
    int start = text.indexOf(key);
    if (start < 0) { if (out) *out = def; return true; }
    start = text.indexOf(String(":"), start);
    if (start < 0) { if (out) *out = def; return false; }
    start++;
    while (start < text.length() && isspace(text.operator char *()[start])) start++;
    int end = start;
    const char *p = text.operator char *();
    while (end < text.length() && (isalpha((unsigned char)p[end]) || isdigit((unsigned char)p[end]))) end++;
    raw = text.substr(start, end - start).trim();
    if (raw == String("true") || raw == String("1")) {
        if (out) *out = true;
        return true;
    }
    if (raw == String("false") || raw == String("0")) {
        if (out) *out = false;
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
    EpaKernel *k = activeKernel();
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

bool EpaDbgService::installBreakpointOverlays(EpaKernel *kernel, std::vector<BreakpointOverlay> &overlays, String &error_message) const {
    uint8_t patch[EPA_DBG_BREAK_OPCODE_LEN];
    overlays.clear();
    if (!kernel || breakpoints.empty()) return true;
    writeLe16(patch, EPA_OP_BREAK);
    writeLe32(patch + 2, EPA_DBG_OVERLAY_BREAK_CODE);
    for (size_t i = 0; i < breakpoints.size(); i++) {
        const Breakpoint &bp = breakpoints[i];
        uint32_t wid = 0;
        if (epa_dbg_any_worker_at(kernel, bp.block_type, bp.block_id, bp.rel_pc, &wid)) {
            continue;
        }
        bool duplicate = false;
        for (size_t j = 0; j < overlays.size(); j++) {
            const Breakpoint &existing = overlays[j].bp;
            if (existing.block_type == bp.block_type && existing.block_id == bp.block_id && existing.rel_pc == bp.rel_pc) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        BreakpointOverlay overlay;
        memset(&overlay, 0, sizeof(overlay));
        overlay.bp = bp;
        overlay.len = EPA_DBG_BREAK_OPCODE_LEN;
        if (!epa_dbg_patch_code(kernel, bp.block_type, bp.block_id, bp.rel_pc, patch, overlay.len, overlay.original)) {
            error_message = String("could not overlay breakpoint at block ")
                          + String((int)bp.block_type) + String(":") + String((int)bp.block_id)
                          + String(" +0x") + String((int)bp.rel_pc);
            restoreBreakpointOverlays(kernel, overlays);
            return false;
        }
        overlay.active = true;
        overlays.push_back(overlay);
    }
    return true;
}

void EpaDbgService::restoreBreakpointOverlays(EpaKernel *kernel, std::vector<BreakpointOverlay> &overlays) const {
    if (!kernel) return;
    for (std::vector<BreakpointOverlay>::reverse_iterator it = overlays.rbegin(); it != overlays.rend(); ++it) {
        if (!it->active) continue;
        epa_dbg_patch_code(kernel, it->bp.block_type, it->bp.block_id, it->bp.rel_pc, it->original, it->len, NULL);
        it->active = false;
    }
}

bool EpaDbgService::translateOverlayBreakEvent(EpaKernel *kernel, const std::vector<BreakpointOverlay> &overlays) {
    if (!kernel || events.empty()) return false;
    DebugEvent &ev = events.back();
    if (ev.kind != String("break") || ev.code != EPA_DBG_OVERLAY_BREAK_CODE) return false;
    for (size_t i = 0; i < overlays.size(); i++) {
        const BreakpointOverlay &overlay = overlays[i];
        const Breakpoint &bp = overlay.bp;
        if (bp.block_type != ev.block_type || bp.block_id != ev.block_id || bp.rel_pc != ev.rel_pc) continue;
        epa_dbg_set_worker_eip(kernel, ev.wid, bp.block_type, bp.block_id, bp.rel_pc);
        ev.kind = String("breakpoint");
        ev.code = 0;
        ev.message = String("breakpoint hit");
        return true;
    }
    return false;
}

std::string EpaDbgService::workerDebugKey(const String &path_id, uint32_t wid) const {
    String pid(path_id);
    return std::string(pid.operator char *()) + ":" + std::to_string((unsigned int)wid);
}

bool EpaDbgService::workerDebugIsEnabled(const String &path_id, uint32_t wid) const {
    std::map<std::string, bool>::const_iterator it = worker_debug_enabled.find(workerDebugKey(path_id, wid));
    if (it == worker_debug_enabled.end()) return true;
    return it->second;
}

void EpaDbgService::setWorkerDebugEnabled(const String &path_id, uint32_t wid, bool enabled) {
    worker_debug_enabled[workerDebugKey(path_id, wid)] = enabled;
}

bool EpaDbgService::runTicks(const String &path_id, uint32_t tick_count, bool stop_on_bp,
                              uint32_t target_wid,
                              String &stop_reason, uint32_t &ticks_ran, String &error_message) {
    EpaKernel *k = kernelForPath(path_id);
    char err[EPA_MAX_ERR];
    if (!k) { error_message = String("kernel not created"); return false; }
    if (target_wid != 0xffffffffu && !workerDebugIsEnabled(path_id, target_wid)) {
        return runToWait(path_id, target_wid, tick_count ? tick_count : 500000u, false, stop_reason, ticks_ran, error_message);
    }
    ensureDebugCallback();
    ticks_ran = 0;
    size_t event_base = events.size();
    std::vector<BreakpointOverlay> overlays;
    if (stop_on_bp && !installBreakpointOverlays(k, overlays, error_message)) {
        stop_reason = String("error");
        return false;
    }
    bool result = true;
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
            if (!startsWith(et, "run: step complete returning to host")) {
                error_message = et.length() ? et : host.lastError();
                stop_reason   = String("error");
                result = false;
                break;
            }
        }
        if (stop_on_bp && events.size() > event_base) translateOverlayBreakEvent(k, overlays);
        if (events.size() > event_base) { stop_reason = events.back().kind; break; }
        if (stop_on_bp) {
            uint32_t wid = 0; Breakpoint bp;
            if (hasBreakpointHit(&wid, &bp)) {
                EpaDbgEip at; at.block_type = bp.block_type; at.block_id = bp.block_id; at.rel_pc = bp.rel_pc;
                pushEvent(String("breakpoint"), wid, 0, &at, String("breakpoint hit"));
                stop_reason = String("breakpoint");
                break;
            }
        }
        if (tick_count != 0 && ticks_ran >= tick_count) { stop_reason = String("step"); break; }
    }
    restoreBreakpointOverlays(k, overlays);
    return result;
}

bool EpaDbgService::runToWait(const String &path_id, uint32_t target_wid, uint32_t max_ticks,
                               bool use_breakpoints,
                               String &stop_reason, uint32_t &ticks_ran, String &error_message) {
    EpaKernel *k = kernelForPath(path_id);
    char err[EPA_MAX_ERR];
    if (!k) { error_message = String("kernel not created"); return false; }
    ensureDebugCallback();
    ticks_ran = 0;
    size_t event_base = events.size();
    std::vector<BreakpointOverlay> overlays;
    if (use_breakpoints && !installBreakpointOverlays(k, overlays, error_message)) {
        stop_reason = String("error");
        return false;
    }
    bool result = true;
    for (;;) {
        EpaDbgWorkerSnapshot initial_ws[EPA_DBG_MAX_WORKERS];
        size_t initial_wc = epa_dbg_capture_workers(k, initial_ws, EPA_DBG_MAX_WORKERS);
        for (size_t i = 0; i < initial_wc; i++) {
            if (initial_ws[i].wid == target_wid) {
                if (initial_ws[i].waiting_for_data) { stop_reason = String("wait_for_data"); goto run_to_wait_done; }
                if (initial_ws[i].blocked)          { stop_reason = String("blocked");       goto run_to_wait_done; }
                if (initial_ws[i].halted)           { stop_reason = String("halted");        goto run_to_wait_done; }
                if (initial_ws[i].faulted)          { stop_reason = String("faulted");       goto run_to_wait_done; }
                break;
            }
        }
        StdoutCapture cap;
        err[0] = 0;
        cap.begin();
        int ok = epa_kernel_run(k, 1u, 1, err);
        String captured = cap.end();
        if (captured.length()) pushLog(captured.trim());
        ticks_ran++;
        if (!ok) {
            String et(err);
            if (!startsWith(et, "run: step complete returning to host")) {
                error_message = et.length() ? et : host.lastError();
                stop_reason = String("error");
                result = false;
                goto run_to_wait_done;
            }
        }
        if (use_breakpoints && events.size() > event_base) translateOverlayBreakEvent(k, overlays);
        EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
        size_t wc = epa_dbg_capture_workers(k, ws, EPA_DBG_MAX_WORKERS);
        for (size_t i = 0; i < wc; i++) {
            if (ws[i].wid == target_wid) {
                if (ws[i].waiting_for_data) { stop_reason = String("wait_for_data"); goto run_to_wait_done; }
                if (ws[i].blocked)          { stop_reason = String("blocked");       goto run_to_wait_done; }
                if (ws[i].halted)           { stop_reason = String("halted");        goto run_to_wait_done; }
                if (ws[i].faulted)          { stop_reason = String("faulted");       goto run_to_wait_done; }
                break;
            }
        }
        if (events.size() > event_base) { stop_reason = events.back().kind; goto run_to_wait_done; }
        if (max_ticks != 0 && ticks_ran >= max_ticks) { stop_reason = String("max_ticks"); goto run_to_wait_done; }
    }
run_to_wait_done:
    restoreBreakpointOverlays(k, overlays);
    return result;
}

bool EpaDbgService::drainNonDebugWorkers(const String &path_id, uint32_t max_ticks,
                                          uint32_t &ticks_ran, String &error_message) {
    EpaKernel *k = kernelForPath(path_id);
    if (!k) { error_message = String("kernel not created"); return false; }
    ticks_ran = 0;
    for (;;) {
        EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
        size_t wc = epa_dbg_capture_workers(k, ws, EPA_DBG_MAX_WORKERS);
        bool did_work = false;
        for (size_t i = 0; i < wc; i++) {
            uint32_t wid = ws[i].wid;
            if (workerDebugIsEnabled(path_id, wid)) continue;
            if (ws[i].waiting_for_data || ws[i].blocked || ws[i].halted || ws[i].faulted) continue;
            uint32_t ran = 0;
            String stop_reason;
            if (!runToWait(path_id, wid, max_ticks ? (max_ticks - ticks_ran) : 0u, false, stop_reason, ran, error_message)) {
                ticks_ran += ran;
                return false;
            }
            ticks_ran += ran;
            did_work = true;
            if (max_ticks != 0 && ticks_ran >= max_ticks) return true;
        }
        if (!did_work) return true;
    }
}

std::string EpaDbgService::blockKey(uint8_t block_type, uint32_t block_id) const {
    return std::to_string((int)block_type) + ":" + std::to_string((unsigned int)block_id);
}

bool EpaDbgService::loadEpaMap(const String &map_path, std::map<std::string, std::vector<MapEntry> > &out_map, String &error_message) const {
    String path_text(map_path);
    FILE *f = fopen(path_text.operator char *(), "r");
    if (!f) {
        error_message = String("could not open map: ") + map_path;
        return false;
    }
    out_map.clear();
    char line[512];
    std::string current_key;
    while (fgets(line, sizeof(line), f)) {
        int block_type = 0;
        unsigned int block_id = 0;
        unsigned int offset = 0;
        int epa_line = 0;
        int e_line = 0;
        int epa_col = 1;
        if (sscanf(line, "B %d %u", &block_type, &block_id) == 2) {
            current_key = blockKey((uint8_t)block_type, (uint32_t)block_id);
            continue;
        }
        if (!current_key.length()) continue;
        int count = sscanf(line, "%u %d %d %d", &offset, &epa_line, &e_line, &epa_col);
        if (count >= 3) {
            MapEntry entry;
            entry.offset = (uint32_t)offset;
            entry.epa_line = epa_line;
            entry.e_line = e_line;
            entry.epa_col = (count >= 4) ? epa_col : 1;
            out_map[current_key].push_back(entry);
        }
    }
    fclose(f);
    error_message = String();
    return true;
}

EpaDbgService::WorkerMarkerState EpaDbgService::markerStateForWorker(
    EpaKernel *kernel,
    const std::map<std::string, std::vector<MapEntry> > &map,
    uint32_t target_wid
) const {
    WorkerMarkerState state;
    memset(&state, 0, sizeof(state));
    state.epa_col = 1;
    if (!kernel) return state;
    EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
    size_t wc = epa_dbg_capture_workers(kernel, ws, EPA_DBG_MAX_WORKERS);
    for (size_t i = 0; i < wc; i++) {
        if (ws[i].wid != target_wid) continue;
        state.valid = true;
        state.wid = ws[i].wid;
        state.block_type = ws[i].eip.block_type;
        state.block_id = ws[i].eip.block_id;
        state.rel_pc = ws[i].eip.rel_pc;
        state.waiting_for_data = ws[i].waiting_for_data != 0;
        state.halted = ws[i].halted != 0;
        state.faulted = ws[i].faulted != 0;
        std::map<std::string, std::vector<MapEntry> >::const_iterator it = map.find(blockKey(state.block_type, state.block_id));
        if (it != map.end() && !it->second.empty()) {
            const MapEntry *best = &it->second.front();
            for (size_t j = 0; j < it->second.size(); j++) {
                if (it->second[j].offset <= state.rel_pc) best = &it->second[j];
                else break;
            }
            state.epa_line = best->epa_line;
            state.e_line = best->e_line;
            state.epa_col = best->epa_col > 0 ? best->epa_col : 1;
        }
        break;
    }
    return state;
}

bool EpaDbgService::boundaryCrossed(bool use_epa_mode, const WorkerMarkerState &before, const WorkerMarkerState &after) const {
    if (!before.valid || !after.valid) return true;
    if (before.block_type != after.block_type || before.block_id != after.block_id) return true;
    if (use_epa_mode) {
        if (before.epa_line > 0 && after.epa_line > 0) return before.epa_line != after.epa_line;
        if (before.epa_line <= 0 && after.epa_line > 0) return true;
    } else {
        if (before.e_line <= 0) return after.e_line > 0;
        if (after.e_line <= 0) return false;
        return before.e_line != after.e_line;
    }
    return before.rel_pc != after.rel_pc;
}

String EpaDbgService::stalledReason(bool use_epa_mode, const WorkerMarkerState &before, const WorkerMarkerState &after) const {
    if (!after.valid) return String("no_selected_worker");
    if (after.faulted) return String("faulted");
    if (after.halted) return String("halted");
    if (after.waiting_for_data && !boundaryCrossed(use_epa_mode, before, after)) return String("waiting_for_data");
    return String();
}

bool EpaDbgService::stepBoundary(
    const String &path_id,
    const String &map_path,
    uint32_t target_wid,
    const String &step_mode,
    uint32_t max_ticks,
    String &stop_reason,
    uint32_t &ticks_ran,
    WorkerMarkerState &out_state,
    String &error_message
) {
    std::map<std::string, std::vector<MapEntry> > map;
    EpaKernel *k = kernelForPath(path_id);
    char err[EPA_MAX_ERR];
    if (!k) { error_message = String("kernel not created"); return false; }
    if (!loadEpaMap(map_path, map, error_message)) return false;
    if (!workerDebugIsEnabled(path_id, target_wid)) {
        bool ok = runToWait(path_id, target_wid, max_ticks, false, stop_reason, ticks_ran, error_message);
        out_state = markerStateForWorker(k, map, target_wid);
        return ok;
    }
    ensureDebugCallback();
    WorkerMarkerState before = markerStateForWorker(k, map, target_wid);
    if (!before.valid) {
        stop_reason = String("no_selected_worker");
        ticks_ran = 0;
        out_state = before;
        return true;
    }
    ticks_ran = 0;
    out_state = before;
    size_t event_base = events.size();
    bool use_epa_mode = false;
    if (step_mode == String("epa")) use_epa_mode = true;
    bool result = true;
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
            if (!startsWith(et, "run: step complete returning to host")) {
                error_message = et.length() ? et : host.lastError();
                stop_reason = String("error");
                result = false;
                break;
            }
        }
        if (events.size() > event_base) {
            stop_reason = events.back().kind;
            break;
        }
        out_state = markerStateForWorker(k, map, target_wid);
        if (boundaryCrossed(use_epa_mode, before, out_state)) {
            stop_reason = String("boundary");
            break;
        }
        String stalled = stalledReason(use_epa_mode, before, out_state);
        if (stalled.length()) {
            stop_reason = stalled;
            break;
        }
        if (max_ticks != 0 && ticks_ran >= max_ticks) {
            stop_reason = String("max_ticks");
            break;
        }
    }
    return result;
}

String EpaDbgService::buildMarkerJson(const WorkerMarkerState &state) const {
    String result("{");
    result += String("\"valid\":") + String(state.valid ? "true" : "false");
    result += String(",\"wid\":") + String((int)state.wid);
    result += String(",\"block_type\":") + String((int)state.block_type);
    result += String(",\"block_id\":") + String((int)state.block_id);
    result += String(",\"rel_pc\":") + String((int)state.rel_pc);
    result += String(",\"epa_line\":") + String(state.epa_line);
    result += String(",\"e_line\":") + String(state.e_line);
    result += String(",\"epa_col\":") + String(state.epa_col);
    result += String(",\"waiting_for_data\":") + String(state.waiting_for_data ? 1 : 0);
    result += String(",\"halted\":") + String(state.halted ? 1 : 0);
    result += String(",\"faulted\":") + String(state.faulted ? 1 : 0);
    result += String("}");
    return result;
}

String EpaDbgService::buildSnapshotJson(const String &path_id) const {
    String result("{");
    EpaDbgKernelSnapshot ks;
    EpaDbgWorkerSnapshot ws[EPA_DBG_MAX_WORKERS];
    size_t wc = 0;
    memset(&ks, 0, sizeof(ks));
    memset(ws, 0, sizeof(ws));
    EpaKernel *snap_kernel = host.rawKernel();
    if (!snap_kernel && host.kernelCount() > 0) {
        int idx = 0;
        if (path_id.length()) {
            int found = host.findKernelIndex(path_id);
            if (found >= 0) idx = found;
        }
        snap_kernel = host.rawKernelAt((size_t)idx);
    }
    if (snap_kernel) {
        epa_dbg_capture_kernel(snap_kernel, &ks);
        wc = epa_dbg_capture_workers(snap_kernel, ws, EPA_DBG_MAX_WORKERS);
    }
    result += String("\"kernel\":{");
    result += String("\"prog_loaded\":") + String((int)ks.prog_loaded);
    result += String(",\"rr_cursor\":") + String((int)ks.rr_cursor);
    result += String(",\"current_wid\":") + String((int)ks.current_wid);
    result += String(",\"interrupt_requested\":") + String((int)ks.interrupt_requested);
    result += String(",\"worker_count\":") + String((int)ks.worker_count);
    result += String(",\"ghs_live_count\":") + String((int)ks.ghs_live_count);
    result += String(",\"ghs_capacity\":") + String((int)ks.ghs_capacity);
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
        result += String(",\"debug_enabled\":") + String(workerDebugIsEnabled(path_id, w.wid) ? "true" : "false");
        result += String(",\"has_current_ghs\":") + String((int)w.has_current_ghs);
        result += String(",\"current_ghs\":") + String((unsigned long long)w.current_ghs);
        result += String(",\"eip\":{\"block_type\":") + String((int)w.eip.block_type)
               + String(",\"block_id\":") + String((int)w.eip.block_id)
               + String(",\"rel_pc\":") + String((int)w.eip.rel_pc) + String("}");
        result += String(",\"regs\":[") + String((int)w.csc[0]) + String(",") + String((int)w.csc[1])
               + String(",") + String((int)w.csc[2]) + String(",") + String((int)w.csc[3]) + String("]");
        result += String(",\"inq_count\":") + String((int)w.inq_count);
        result += String(",\"outq_count\":") + String((int)w.outq_count);
        result += String(",\"owned_ghs_count\":") + String((int)w.owned_ghs_count);
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
        result += String(",\"fault_message\":") + jq(String(w.fault_message));
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

String EpaDbgService::buildWorkerInspectJson(const String &path_id, uint32_t wid,
                                             uint32_t stack_words, uint32_t arena_bytes,
                                             uint32_t ghs_bytes, String &error_message) const {
    EpaKernel *k = kernelForPath(path_id);
    EpaDbgWorkerInspect info;
    memset(&info, 0, sizeof(info));
    if (!k) {
        error_message = String("kernel not created");
        return String();
    }
    if (!epa_dbg_capture_worker_inspect(k, wid, &info, stack_words, arena_bytes, ghs_bytes)) {
        error_message = String("worker inspect unavailable");
        return String();
    }

    String result("{");
    result += String("\"wid\":") + String((int)info.wid);
    result += String(",\"eip\":{\"block_type\":") + String((int)info.eip.block_type)
           + String(",\"block_id\":") + String((int)info.eip.block_id)
           + String(",\"rel_pc\":") + String((int)info.eip.rel_pc) + String("}");
    result += String(",\"regs\":[") + String((int)info.csc[0]) + String(",")
           + String((int)info.csc[1]) + String(",")
           + String((int)info.csc[2]) + String(",")
           + String((int)info.csc[3]) + String("]");
    result += String(",\"queues\":{\"inq_count\":") + String((int)info.inq_count)
           + String(",\"outq_count\":") + String((int)info.outq_count) + String("}");
    result += String(",\"flags\":{\"halted\":") + String((int)info.halted)
           + String(",\"blocked\":") + String((int)info.blocked)
           + String(",\"faulted\":") + String((int)info.faulted)
           + String(",\"waiting_for_data\":") + String((int)info.waiting_for_data) + String("}");
    result += String(",\"stack\":{\"depth\":") + String((int)info.stack_depth)
           + String(",\"start_index\":") + String((int)info.stack_start)
           + String(",\"words\":[");
    for (uint32_t i = 0; i < info.stack_word_count; i++) {
        if (i) result += String(",");
        result += String((int)info.stack_words[i]);
    }
    result += String("]}");
    result += String(",\"locals\":{\"count\":") + String((int)EPA_DBG_LOCALS) + String(",\"values\":[");
    for (uint32_t i = 0; i < EPA_DBG_LOCALS; i++) {
        if (i) result += String(",");
        result += String((int)info.locals[i]);
    }
    result += String("]}");
    result += String(",\"local_arena\":{\"top\":") + String((int)info.lbytes_top)
           + String(",\"cap\":") + String((int)info.lbytes_cap)
           + String(",\"scope_depth\":") + String((int)info.lscope_depth)
           + String(",\"preview_from\":") + String((int)info.arena_preview_from)
           + String(",\"preview_len\":") + String((int)info.arena_preview_len)
           + String(",\"preview_hex\":") + jq(hexBytes(info.arena_preview, info.arena_preview_len)) + String("}");
    result += String(",\"ghs\":{\"present\":") + String(info.has_current_ghs ? "true" : "false")
           + String(",\"handle\":") + String((unsigned long long)info.current_ghs)
           + String(",\"live_count\":") + String((int)info.ghs_live_count)
           + String(",\"capacity\":") + String((int)info.ghs_capacity)
           + String(",\"valid\":") + String(info.ghs.valid ? "true" : "false");
    if (info.ghs.valid) {
        result += String(",\"meta\":{\"type\":") + String((int)info.ghs.type)
               + String(",\"type_name\":") + jq(String(ghsTypeName((uint32_t)info.ghs.type)))
               + String(",\"owner\":") + String((int)info.ghs.owner)
               + String(",\"flags\":") + String((int)info.ghs.flags)
               + String(",\"size_bytes\":") + String((int)info.ghs.size_bytes)
               + String(",\"capacity\":") + String((int)info.ghs.capacity)
               + String(",\"generation\":") + String((int)info.ghs.generation) + String("}");
        result += String(",\"preview_len\":") + String((int)info.ghs.preview_len)
               + String(",\"preview_hex\":") + jq(hexBytes(info.ghs.preview, info.ghs.preview_len));
    }
    result += String("}");
    result += String("}");
    return result;
}

bool EpaDbgService::call(const String &method, const String &params_json,
                          String &result_json, String &error_code, String &error_message) {

    if (method == String("ping")) {
        result_json = String("{\"message\":\"pong\"}"); return true;
    }
    if (method == String("debug.create")) {
        if (!host.create()) { error_code = String("create_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallback();
        ensureSignalCallback();
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        result_json = String("{\"created\":true}");
        return true;
    }
    if (method == String("debug.destroy")) {
        host.destroy();
        events.clear();
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        worker_debug_enabled.clear();
        result_json = String("{\"destroyed\":true}");
        return true;
    }
    if (method == String("debug.setKernelId")) {
        String id;
        if (!parseString(params_json, String("kernel_id"), id)) id = String("epa.debug.kernel");
        if (!host.setKernelId(id)) { error_code = String("set_kernel_id_failed"); error_message = host.lastError(); return false; }
        result_json = String("{\"ok\":true}"); return true;
    }
    if (method == String("debug.loadAsm")) {
        String path;
        if (!parseString(params_json, String("asm_path"), path) || !path.length()) {
            error_code = String("missing_asm_path"); error_message = String("asm_path is required"); return false;
        }
        if (!host.loadAsmPath(path)) { error_code = String("load_asm_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallback();
        ensureSignalCallback();
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        result_json = String("{\"loaded\":true}");
        return true;
    }
    if (method == String("debug.loadBundle")) {
        String path;
        if (!parseString(params_json, String("bundle_path"), path) || !path.length()) {
            error_code = String("missing_bundle_path"); error_message = String("bundle_path is required"); return false;
        }
        if (!host.loadBundlePath(path)) { error_code = String("load_bundle_failed"); error_message = host.lastError(); return false; }
        ensureDebugCallback();
        ensureSignalCallback();
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        result_json = String("{\"loaded\":true}");
        return true;
    }
    if (method == String("debug.clearMailbox")) {
        last_mailbox_bytes.clear();
        last_mailbox_wid = 0;
        {
            EpaKernel *kernel = activeKernel();
            if (kernel) epa_dbg_clear_last_host_signal(kernel);
            for (size_t i = 0; i < host.kernelCount(); i++) {
                EpaKernel *mk = host.rawKernelAt(i);
                if (mk) epa_dbg_clear_last_host_signal(mk);
            }
        }
        result_json = String("{\"cleared\":true}");
        return true;
    }
    if (method == String("debug.ingressPushHex")) {
        uint32_t wid = 1, tag = 0;
        String hex, path_id;
        std::vector<unsigned char> bytes;
        parseUint(params_json, String("wid"), 1, &wid);
        parseUint(params_json, String("tag"), 0, &tag);
        parseString(params_json, String("path_id"), path_id);
        if (!parseString(params_json, String("payload_hex"), hex) || !parseHexBytes(hex, bytes)) {
            error_code = String("invalid_payload_hex"); error_message = String("payload_hex must be an even-length hex string"); return false;
        }
        bool ok = false;
        if (host.rawKernel()) {
            ok = host.ingressPushTagged(wid, tag, bytes.data(), (uint32_t)bytes.size());
        } else if (host.kernelCount() > 0) {
            int idx = 0;
            if (path_id.length()) {
                int found = host.findKernelIndex(path_id);
                if (found >= 0) idx = found;
            }
            ok = host.ingressPushTaggedToKernel((size_t)idx, wid, tag, bytes.data(), (uint32_t)bytes.size());
        } else {
            error_code = String("ingress_push_failed"); error_message = String("no kernel or module loaded"); return false;
        }
        if (!ok) { error_code = String("ingress_push_failed"); error_message = host.lastError(); return false; }
        result_json = String("{\"queued\":true}"); return true;
    }
    if (method == String("debug.step")) {
        uint32_t ticks = 1, ticks_ran = 0, drain_ticks = 0, wid = 0xffffffffu;
        String stop_reason;
        String path_id;
        parseUint(params_json, String("ticks"), 1, &ticks);
        parseUint(params_json, String("wid"), 0xffffffffu, &wid);
        parseString(params_json, String("path_id"), path_id);
        if (!runTicks(path_id, ticks, false, wid, stop_reason, ticks_ran, error_message)) {
            error_code = String("step_failed"); return false;
        }
        if (!drainNonDebugWorkers(path_id, 500000u, drain_ticks, error_message)) {
            error_code = String("drain_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"drain_ticks_ran\":") + String((int)drain_ticks)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson(path_id) + String("}");
        return true;
    }
    if (method == String("debug.run")) {
        uint32_t max_ticks = 1000, ticks_ran = 0, drain_ticks = 0;
        String stop_reason;
        String path_id;
        parseUint(params_json, String("max_ticks"), 1000, &max_ticks);
        parseString(params_json, String("path_id"), path_id);
        if (!runTicks(path_id, max_ticks, true, 0xffffffffu, stop_reason, ticks_ran, error_message)) {
            error_code = String("run_failed"); return false;
        }
        if (!drainNonDebugWorkers(path_id, 500000u, drain_ticks, error_message)) {
            error_code = String("drain_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"drain_ticks_ran\":") + String((int)drain_ticks)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson(path_id) + String("}");
        return true;
    }
    if (method == String("debug.runToWait")) {
        uint32_t wid = 0, max_ticks = 500000, ticks_ran = 0, drain_ticks = 0;
        String stop_reason;
        String path_id;
        parseUint(params_json, String("wid"),       0,      &wid);
        parseUint(params_json, String("max_ticks"), 500000, &max_ticks);
        parseString(params_json, String("path_id"), path_id);
        if (!runToWait(path_id, wid, max_ticks, true, stop_reason, ticks_ran, error_message)) {
            error_code = String("run_to_wait_failed"); return false;
        }
        if (!drainNonDebugWorkers(path_id, 500000u, drain_ticks, error_message)) {
            error_code = String("drain_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"drain_ticks_ran\":") + String((int)drain_ticks)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"snapshot\":") + buildSnapshotJson(path_id) + String("}");
        return true;
    }
    if (method == String("debug.stepBoundary")) {
        uint32_t wid = 0, max_ticks = 4096, ticks_ran = 0, drain_ticks = 0;
        String path_id, map_path, step_mode, stop_reason;
        WorkerMarkerState marker;
        parseUint(params_json, String("wid"), 0, &wid);
        parseUint(params_json, String("max_ticks"), 4096, &max_ticks);
        parseString(params_json, String("path_id"), path_id);
        parseString(params_json, String("map_path"), map_path);
        parseString(params_json, String("step_mode"), step_mode);
        if (String(params_json).indexOf(String("\"step_mode\":\"epa\"")) >= 0
            || String(params_json).indexOf(String("\"step_mode\": \"epa\"")) >= 0) {
            step_mode = String("epa");
        } else if (String(params_json).indexOf(String("\"step_mode\":\"e\"")) >= 0
                   || String(params_json).indexOf(String("\"step_mode\": \"e\"")) >= 0) {
            step_mode = String("e");
        }
        if (!map_path.length()) {
            error_code = String("missing_map_path");
            error_message = String("map_path is required");
            return false;
        }
        if (!stepBoundary(path_id, map_path, wid, step_mode, max_ticks, stop_reason, ticks_ran, marker, error_message)) {
            error_code = String("step_boundary_failed");
            return false;
        }
        if (!drainNonDebugWorkers(path_id, 500000u, drain_ticks, error_message)) {
            error_code = String("drain_failed"); return false;
        }
        result_json = String("{\"ticks_ran\":") + String((int)ticks_ran)
                    + String(",\"drain_ticks_ran\":") + String((int)drain_ticks)
                    + String(",\"stop_reason\":") + jq(stop_reason)
                    + String(",\"marker\":") + buildMarkerJson(marker)
                    + String(",\"snapshot\":") + buildSnapshotJson(path_id) + String("}");
        return true;
    }
    if (method == String("debug.setWorkerDebug")) {
        uint32_t wid = 0;
        bool enabled = true;
        String path_id;
        parseUint(params_json, String("wid"), 0, &wid);
        parseBool(params_json, String("enabled"), true, &enabled);
        parseString(params_json, String("path_id"), path_id);
        setWorkerDebugEnabled(path_id, wid, enabled);
        uint32_t drain_ticks = 0;
        if (!enabled && kernelForPath(path_id)) {
            if (!drainNonDebugWorkers(path_id, 500000u, drain_ticks, error_message)) {
                error_code = String("drain_failed"); return false;
            }
        }
        result_json = String("{\"path_id\":") + jq(path_id)
                    + String(",\"wid\":") + String((int)wid)
                    + String(",\"debug_enabled\":") + String(enabled ? "true" : "false")
                    + String(",\"drain_ticks_ran\":") + String((int)drain_ticks)
                    + String(",\"snapshot\":") + buildSnapshotJson(path_id)
                    + String("}");
        return true;
    }
    if (method == String("debug.getWorkerDebug")) {
        uint32_t wid = 0;
        String path_id;
        parseUint(params_json, String("wid"), 0, &wid);
        parseString(params_json, String("path_id"), path_id);
        bool enabled = workerDebugIsEnabled(path_id, wid);
        result_json = String("{\"path_id\":") + jq(path_id)
                    + String(",\"wid\":") + String((int)wid)
                    + String(",\"debug_enabled\":") + String(enabled ? "true" : "false")
                    + String("}");
        return true;
    }
    if (method == String("debug.interrupt")) {
        EpaKernel *k = host.rawKernel();
        if (k) epa_kernel_request_interrupt(k);
        result_json = String("{\"interrupt_requested\":true}"); return true;
    }
    if (method == String("debug.snapshot")) {
        String path_id;
        parseString(params_json, String("path_id"), path_id);
        result_json = buildSnapshotJson(path_id); return true;
    }
    if (method == String("debug.inspectWorker")) {
        uint32_t wid = 0, stack_words = 32, arena_bytes = 128, ghs_bytes = 128;
        String path_id;
        parseUint(params_json, String("wid"), 0, &wid);
        parseUint(params_json, String("stack_words"), 32, &stack_words);
        parseUint(params_json, String("arena_bytes"), 128, &arena_bytes);
        parseUint(params_json, String("ghs_bytes"), 128, &ghs_bytes);
        parseString(params_json, String("path_id"), path_id);
        result_json = buildWorkerInspectJson(path_id, wid, stack_words, arena_bytes, ghs_bytes, error_message);
        if (!result_json.length()) {
            error_code = String("inspect_worker_failed");
            if (!error_message.length()) error_message = String("worker inspect failed");
            return false;
        }
        return true;
    }
    if (method == String("debug.events")) {
        bool clear = true;
        parseBool(params_json, String("clear"), true, &clear);
        result_json = buildEventsJson(clear); return true;
    }
    if (method == String("debug.getMailbox")) {
        String path_id;
        String hex;
        char tmp[3];
        std::vector<uint8_t> bytes;
        EpaKernel *kernel = NULL;
        parseString(params_json, String("path_id"), path_id);
        kernel = kernelForPath(path_id);
        if (kernel) {
            uint32_t len = epa_dbg_last_host_signal_len(kernel);
            if (len > 0u) {
                bytes.resize(len);
                if (epa_dbg_copy_last_host_signal(kernel, bytes.data(), len) != (int)len) {
                    error_code = String("mailbox_copy_failed");
                    error_message = String("could not copy host signal mailbox");
                    return false;
                }
            }
        }
        for (size_t i = 0; i < bytes.size(); i++) {
            snprintf(tmp, sizeof(tmp), "%02x", (unsigned)bytes[i]);
            hex += String(tmp);
        }
        result_json = String("{\"wid\":") + String((int)(kernel ? epa_dbg_last_host_signal_wid(kernel) : 0u))
                    + String(",\"len\":") + String((int)bytes.size())
                    + String(",\"hex\":\"") + hex + String("\"}");
        return true;
    }
    if (method == String("debug.breakpointAdd")) {
        Breakpoint bp; bp.block_type = 0; bp.block_id = 0; bp.rel_pc = 0;
        uint32_t addr = 0;
        parseUint(params_json, String("block_type"), 0, (uint32_t *)&bp.block_type);
        parseUint(params_json, String("block_id"),   0, (uint32_t *)&bp.block_id);
        parseUint(params_json, String("rel_pc"),     0, &bp.rel_pc);
        if (parseUint(params_json, String("addr"), bp.rel_pc, &addr)) bp.rel_pc = addr;
        bool exists = false;
        for (size_t i = 0; i < breakpoints.size(); i++) {
            if (breakpoints[i].block_type == bp.block_type &&
                breakpoints[i].block_id == bp.block_id &&
                breakpoints[i].rel_pc == bp.rel_pc) {
                exists = true;
                break;
            }
        }
        if (!exists) breakpoints.push_back(bp);
        result_json = buildBreakpointJson(); return true;
    }
    if (method == String("debug.breakpointClear")) {
        uint32_t bt = 0, bi = 0, rpc = 0;
        uint32_t addr = 0;
        String ptext(params_json);
        bool has_location = (
            ptext.indexOf(String("\"block_type\"")) >= 0 ||
            ptext.indexOf(String("\"block_id\"")) >= 0 ||
            ptext.indexOf(String("\"rel_pc\"")) >= 0 ||
            ptext.indexOf(String("\"addr\"")) >= 0
        );
        if (!has_location) {
            breakpoints.clear();
            result_json = buildBreakpointJson(); return true;
        }
        parseUint(params_json, String("block_type"), 0, &bt);
        parseUint(params_json, String("block_id"),   0, &bi);
        parseUint(params_json, String("rel_pc"),     0, &rpc);
        if (parseUint(params_json, String("addr"), rpc, &addr)) rpc = addr;
        for (std::vector<Breakpoint>::iterator it = breakpoints.begin(); it != breakpoints.end(); ) {
            if (it->block_type == bt && it->block_id == bi && it->rel_pc == rpc)
                it = breakpoints.erase(it);
            else ++it;
        }
        result_json = buildBreakpointJson(); return true;
    }
    if (method == String("debug.breakpointList")) {
        result_json = buildBreakpointJson(); return true;
    }

    error_code    = String("unknown_method");
    error_message = String("Unknown EPA debug RPC method: ") + method;
    return false;
}

} // namespace elara

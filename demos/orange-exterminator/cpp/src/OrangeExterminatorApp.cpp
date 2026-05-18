#include "OrangeExterminatorApp.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>
#include "OrangeExterminatorEpaDebugShim.h"

namespace elara {
using namespace elara::ui::rpc;

namespace {

static OrangeExterminatorApp *g_orange_exterminator_app = NULL;

static uint32_t read_le_u32(const unsigned char *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

struct SceneViewState {
    int lane;
    int depth;
};

class UiEventSinkService : public sockets::rpc::json::JsonRPCService {
public:
    explicit UiEventSinkService(OrangeExterminatorApp *value_app)
        : sockets::rpc::json::JsonRPCService("ui"),
          app(value_app) {
    }

    bool call(
        const String& method,
        const String& params_json,
        String& result_json,
        String& error_code,
        String& error_message
    ) {
        if (method == String("event")) {
            String params = params_json;
            if (app && params.indexOf(String("\"action\":\"keyDown\"")) >= 0) {
                int key_index = params.indexOf(String("\"keyval\":"));
                if (key_index >= 0) {
                    String fragment = params.substr(key_index + 9).trim();
                    unsigned int keyval = (unsigned int)strtoul(fragment.operator char *(), NULL, 10);
                    app->enqueueKeyDown(keyval);
                }
            }
            result_json = "{\"received\":true}";
            return true;
        }

        error_code = "method_not_found";
        error_message = "No client-side ui event handler matched the request";
        return false;
    }

private:
    OrangeExterminatorApp *app;
};

SceneViewState readSceneViewState(const OrangeExterminatorEpaVmHost &epa) {
    SceneViewState state = { 0, 0 };
    int scene_kernel = epa.findKernelIndex(String("scene"));
    if (scene_kernel >= 0) {
        EpaKernel *kernel = epa.rawKernelAt((size_t)scene_kernel);
        if (kernel) {
            OrangeExterminatorEpaDebugWorkerSnapshot workers[ORANGEEXTERMINATOR_EPA_DEBUG_MAX_WORKERS];
            size_t count = OrangeExterminator_epa_debug_capture_workers(kernel, workers, ORANGEEXTERMINATOR_EPA_DEBUG_MAX_WORKERS);
            for (size_t i = 0; i < count; i++) {
                if (workers[i].wid != 1u) {
                    continue;
                }
                state.lane = workers[i].locals[1];
                state.depth = workers[i].locals[2];
                break;
            }
        }
    }
    return state;
}

int on_surface_host_signal(uint8_t wid, const char *msg, const int msg_len) {
    if (g_orange_exterminator_app) {
        g_orange_exterminator_app->updateSurfaceCommandsFromMailbox((unsigned int)wid, msg, msg_len);
    }
    return 1;
}

}

OrangeExterminatorApp::OrangeExterminatorApp(const String &value_host, int value_port)
    : host(value_host),
      port(value_port),
      bundle_path(String("..") + String("/") + String("build") + String("/") + String("epa.bin")),
      bundle_exists(false),
      epa_loaded(false),
      epa_started(false),
      incremental_ui_supported(true),
      input_lock("orange-exterminator-input"),
      render_lock("orange-exterminator-render"),
      latest_surface_valid(false),
      trace_path(String("..") + String("/") + String("artifacts") + String("/") + String("live-epa-trace.jsonl")),
      trace_file(NULL),
      trace_sequence(0),
      peer(new ElaraUiRpcPeer()) {
}

void OrangeExterminatorApp::openTraceArtifact() {
    String artifact_dir = String("..") + String("/") + String("artifacts");
    mkdir(artifact_dir.operator char *(), 0777);
    if (trace_file) {
        fclose(trace_file);
        trace_file = NULL;
    }
    trace_file = fopen(trace_path.operator char *(), "w");
    if (!trace_file) {
        printf("Failed to open EPA trace artifact: %s\n", trace_path.operator char *());
        return;
    }
    trace_sequence = 0;
    traceLine(String("{\"event\":\"trace_open\",\"path\":") + JsonString(trace_path, true).toString() + String("}"));
}

void OrangeExterminatorApp::closeTraceArtifact() {
    if (!trace_file) {
        return;
    }
    traceLine(String("{\"event\":\"trace_close\"}"));
    fclose(trace_file);
    trace_file = NULL;
}

void OrangeExterminatorApp::armUiInputFocus() {
    String result_json;
    String error_code;
    String error_message;

    peer->call(String("ui.enableEvent"), String("{\"action\":\"keyDown\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"keyUp\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.setFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);

    traceLine(String("{\"event\":\"ui_input_focus_armed\",\"target\":\"app.surface\"}"));
}

void OrangeExterminatorApp::traceLine(const String &json_line) {
    time_t now;
    struct tm tm_now;
    char timestamp[64];
    String line_copy(json_line);
    if (!trace_file) {
        return;
    }
    now = time(NULL);
    memset(&tm_now, 0, sizeof(tm_now));
    localtime_r(&now, &tm_now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);
    fprintf(trace_file, "{\"seq\":%lu,\"ts\":\"%s\",\"data\":%s}\n",
            trace_sequence++,
            timestamp,
            line_copy.operator char *());
    fflush(trace_file);
}

void OrangeExterminatorApp::traceKernelStateSnapshot(const char *phase) {
    size_t i;
    if (!epa_loaded) {
        traceLine(String("{\"event\":\"kernel_snapshot\",\"phase\":")
            + JsonString(String(phase ? phase : "unknown"), true).toString()
            + String(",\"ready\":false}"));
        return;
    }
    for (i = 0; i < epa.kernelCount(); i++) {
        EpaKernel *kernel = epa.rawKernelAt(i);
        OrangeExterminatorEpaDebugKernelSnapshot kernel_snapshot;
        OrangeExterminatorEpaDebugWorkerSnapshot workers[ORANGEEXTERMINATOR_EPA_DEBUG_MAX_WORKERS];
        size_t worker_count = 0;
        String line;
        if (!kernel) {
            continue;
        }
        memset(&kernel_snapshot, 0, sizeof(kernel_snapshot));
        OrangeExterminator_epa_debug_capture_kernel(kernel, &kernel_snapshot);
        worker_count = OrangeExterminator_epa_debug_capture_workers(kernel, workers, ORANGEEXTERMINATOR_EPA_DEBUG_MAX_WORKERS);
        line = String("{\"event\":\"kernel_snapshot\",\"phase\":")
             + JsonString(String(phase ? phase : "unknown"), true).toString()
             + String(",\"kernel_index\":") + String((int)i)
             + String(",\"kernel_id\":") + JsonString(epa.kernelPathId(i), true).toString()
             + String(",\"status\":") + JsonString(epa.kernelStatus(i), true).toString()
             + String(",\"thread_count\":") + String((int)epa.kernelThreadCount(i))
             + String(",\"rr_cursor\":") + String((int)kernel_snapshot.rr_cursor)
             + String(",\"current_wid\":") + String((int)kernel_snapshot.current_wid)
             + String(",\"interrupt_requested\":") + String((int)kernel_snapshot.interrupt_requested)
             + String(",\"workers\":[");
        for (size_t w = 0; w < worker_count; w++) {
            if (w > 0) {
                line += String(",");
            }
            line += String("{\"wid\":") + String((int)workers[w].wid)
                 + String(",\"halted\":") + String((int)workers[w].halted)
                 + String(",\"blocked\":") + String((int)workers[w].blocked)
                 + String(",\"faulted\":") + String((int)workers[w].faulted)
                 + String(",\"waiting_for_data\":") + String((int)workers[w].waiting_for_data)
                 + String(",\"has_current_ghs\":") + String((int)workers[w].has_current_ghs)
                 + String(",\"inq_count\":") + String((int)workers[w].inq_count)
                 + String(",\"outq_count\":") + String((int)workers[w].outq_count)
                 + String(",\"stack_depth\":") + String((int)workers[w].stack_depth)
                 + String(",\"lbytes_top\":") + String((int)workers[w].lbytes_top)
                 + String(",\"eip\":{\"block_type\":") + String((int)workers[w].eip.block_type)
                 + String(",\"block_id\":") + String((int)workers[w].eip.block_id)
                 + String(",\"rel_pc\":") + String((int)workers[w].eip.rel_pc)
                 + String("},\"locals\":[")
                 + String((int)workers[w].locals[0]) + String(",")
                 + String((int)workers[w].locals[1]) + String(",")
                 + String((int)workers[w].locals[2]) + String(",")
                 + String((int)workers[w].locals[3]) + String("]}");
        }
        line += String("]}");
        traceLine(line);
    }
}

void OrangeExterminatorApp::refreshProjectState() {
    bundle_exists = access(bundle_path.operator char *(), F_OK) == 0;
}

void OrangeExterminatorApp::refreshEpaState() {
    size_t i;
    refreshProjectState();
    epa_loaded = false;
    epa_started = false;
    epa.destroy();
    {
        Mutex::Lock lock(render_lock);
        latest_surface_valid = false;
        latest_surface_commands = String();
    }
    traceLine(String("{\"event\":\"refresh_epa_state\",\"bundle_exists\":") + String(bundle_exists ? "true" : "false") + String("}"));
    if (!bundle_exists) {
        return;
    }
    if (!epa.loadBundlePath(bundle_path)) {
        traceLine(String("{\"event\":\"bundle_load_failed\",\"error\":") + JsonString(epa.lastError(), true).toString() + String("}"));
        return;
    }
    epa_loaded = true;
    traceLine(String("{\"event\":\"bundle_loaded\",\"kernel_count\":") + String((int)epa.kernelCount()) + String("}"));
    installSurfaceCallback();
    if (!epa.startAllKernels()) {
        traceLine(String("{\"event\":\"start_all_failed\",\"error\":") + JsonString(epa.lastError(), true).toString() + String("}"));
        return;
    }
    epa_started = true;
    printf("EPA module started: kernel_count=%d\n", (int)epa.kernelCount());
    for (i = 0; i < epa.kernelCount(); i++) {
        printf("  kernel[%d] id=%s status=%s threads=%u err=%s\n",
               (int)i,
               epa.kernelPathId(i).operator char *(),
               epa.kernelStatus(i).operator char *(),
               (unsigned)epa.kernelThreadCount(i),
               epa.kernelError(i).operator char *());
    }
    traceKernelStateSnapshot("after_start_all");
}

void OrangeExterminatorApp::stimulateEpa() {
    struct FrameTickPayload {
        uint32_t frame_id;
        uint32_t phase;
        uint32_t mode;
    };
    struct KeyInputPayload {
        uint32_t key_code;
        uint32_t pressed;
        uint32_t modifiers;
    };
    struct PlayerIntentPayload {
        uint32_t move_x;
        uint32_t move_y;
        uint32_t fire_mode;
        uint32_t look_dx;
    };
    struct WeaponCommandPayload {
        uint32_t mode;
        uint32_t trigger;
        uint32_t ammo_hint;
    };
    struct ActorStatePayload {
        uint32_t actor_id;
        uint32_t posture;
        uint32_t flags;
    };
    struct WorldStatePayload {
        uint32_t zone_id;
        uint32_t dirty_flags;
        uint32_t threat_level;
    };

    if (!epa_started) {
        return;
    }
    traceLine(String("{\"event\":\"stimulate_begin\"}"));

    FrameTickPayload tick = { 1u, 0u, 0u };
    KeyInputPayload input = { 0u, 0u, 0u };
    PlayerIntentPayload intent = { 1u, 0u, 1u, 2u };
    WeaponCommandPayload weapon = { 1u, 1u, 30u };
    ActorStatePayload actor = { 7u, 0u, 1u };
    WorldStatePayload world = { 1u, 0u, 2u };

    int idx;

    idx = epa.findKernelIndex(String("entry"));
    if (idx >= 0) {
        traceLine(String("{\"event\":\"ingress_push\",\"kernel\":\"entry\",\"wid\":1,\"ok\":")
            + String(epa.ingressPushToKernel((size_t)idx, 1u, &tick, sizeof(tick)) ? "true" : "false") + String("}"));
    }

    idx = epa.findKernelIndex(String("gameplay_rules"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &tick, sizeof(tick));
        epa.ingressPushToKernel((size_t)idx, 2u, &intent, sizeof(intent));
        epa.ingressPushToKernel((size_t)idx, 3u, &actor, sizeof(actor));
    }

    idx = epa.findKernelIndex(String("input_dispatch"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &input, sizeof(input));
        epa.ingressPushToKernel((size_t)idx, 2u, &tick, sizeof(tick));
    }

    idx = epa.findKernelIndex(String("player_avatar"));
    if (idx >= 0) {
        int ok1 = epa.ingressPushToKernel((size_t)idx, 1u, &input, sizeof(input)) ? 1 : 0;
        printf("stimulate player_avatar worker1=%d\n", ok1);
        traceLine(String("{\"event\":\"ingress_push_batch\",\"kernel\":\"player_avatar\",\"wid1\":")
            + String(ok1) + String(",\"wid2\":0,\"wid3\":0}"));
    }

    idx = epa.findKernelIndex(String("scene"));
    if (idx >= 0) {
        int ok1 = epa.ingressPushToKernel((size_t)idx, 1u, &input, sizeof(input)) ? 1 : 0;
        printf("stimulate scene worker1=%d\n", ok1);
        traceLine(String("{\"event\":\"ingress_push_batch\",\"kernel\":\"scene\",\"wid1\":")
            + String(ok1) + String(",\"wid2\":0,\"wid3\":0}"));
    }

    idx = epa.findKernelIndex(String("player_machinegun"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &weapon, sizeof(weapon));
        epa.ingressPushToKernel((size_t)idx, 2u, &tick, sizeof(tick));
    }

    idx = epa.findKernelIndex(String("world_runtime"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &tick, sizeof(tick));
        epa.ingressPushToKernel((size_t)idx, 2u, &actor, sizeof(actor));
    }

    idx = epa.findKernelIndex(String("render_scene"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &tick, sizeof(tick));
        epa.ingressPushToKernel((size_t)idx, 2u, &actor, sizeof(actor));
        epa.ingressPushToKernel((size_t)idx, 3u, &world, sizeof(world));
    }

    idx = epa.findKernelIndex(String("render_ui"));
    if (idx >= 0) {
        epa.ingressPushToKernel((size_t)idx, 1u, &input, sizeof(input));
        epa.ingressPushToKernel((size_t)idx, 2u, &weapon, sizeof(weapon));
        epa.ingressPushToKernel((size_t)idx, 3u, &tick, sizeof(tick));
    }
    traceKernelStateSnapshot("after_stimulate");
}

void OrangeExterminatorApp::enqueueKeyDown(unsigned int keyval) {
    Mutex::Lock lock(input_lock);
    pending_keydowns.push(keyval);
}

void OrangeExterminatorApp::drainKeyEvents() {
    struct KeyInputPayload {
        uint32_t key_code;
        uint32_t pressed;
        uint32_t modifiers;
    };
    Array<unsigned int> keys;
    int idx;

    {
        Mutex::Lock lock(input_lock);
        keys = pending_keydowns;
        pending_keydowns.clear();
    }

    if (keys.length() <= 0 || !epa_started) {
        return;
    }

    idx = epa.findKernelIndex(String("scene"));
    if (idx < 0) {
        return;
    }

    for (int i = 0; i < (int)keys.length(); i++) {
        unsigned int keyval = keys[i];
        if (keyval != 65361u && keyval != 65362u && keyval != 65363u && keyval != 65364u) {
            continue;
        }
        KeyInputPayload input = { keyval, 1u, 0u };
        {
            bool ok = epa.ingressPushToKernel((size_t)idx, 1u, &input, sizeof(input));
            traceLine(String("{\"event\":\"key_ingress\",\"kernel\":\"scene\",\"wid\":1,\"keyval\":")
                + String((int)keyval) + String(",\"ok\":") + String(ok ? "true" : "false") + String("}"));
        }
    }
    traceKernelStateSnapshot("after_key_ingress");
}

void OrangeExterminatorApp::installSurfaceCallback() {
    int idx = epa.findKernelIndex(String("scene"));
    if (idx >= 0) {
        EpaKernel *kernel = epa.rawKernelAt((size_t)idx);
        if (kernel) {
            g_orange_exterminator_app = this;
            epa_kernel_set_signal_callback(kernel, on_surface_host_signal);
            printf("Installed surface callback on kernel scene idx=%d\n", idx);
            traceLine(String("{\"event\":\"surface_callback_installed\",\"kernel\":\"scene\",\"index\":") + String(idx) + String("}"));
        } else {
            printf("Surface callback install failed: scene kernel pointer null\n");
            traceLine(String("{\"event\":\"surface_callback_install_failed\",\"reason\":\"null_kernel\"}"));
        }
    } else {
        printf("Surface callback install failed: scene kernel not found\n");
        traceLine(String("{\"event\":\"surface_callback_install_failed\",\"reason\":\"kernel_not_found\"}"));
    }
}

void OrangeExterminatorApp::updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len) {
    const unsigned char *bytes = (const unsigned char *)msg;
    size_t offset = 0;
    String json("[");
    int emitted = 0;
    printf("surface mailbox callback wid=%u len=%d\n", wid, msg_len);
    traceLine(String("{\"event\":\"mailbox_callback\",\"wid\":") + String((int)wid) + String(",\"len\":") + String(msg_len) + String("}"));
    if (wid != 1u || !msg || msg_len < 28) {
        printf("surface mailbox ignored: invalid wid/msg/len\n");
        traceLine(String("{\"event\":\"mailbox_ignored\",\"reason\":\"invalid_wid_or_len\"}"));
        return;
    }
    if (read_le_u32(bytes) != 0x45465231u) {
        printf("surface mailbox ignored: bad magic 0x%08x\n", (unsigned)read_le_u32(bytes));
        traceLine(String("{\"event\":\"mailbox_ignored\",\"reason\":\"bad_magic\",\"magic\":") + String((int)read_le_u32(bytes)) + String("}"));
        return;
    }
    offset += 4; /* magic */
    offset += 4; /* version */
    if ((size_t)msg_len < offset + 20u) {
        return;
    }
    {
        uint32_t width = read_le_u32(bytes + offset); offset += 4;
        uint32_t height = read_le_u32(bytes + offset); offset += 4;
        uint32_t clear_r = read_le_u32(bytes + offset); offset += 4;
        uint32_t clear_g = read_le_u32(bytes + offset); offset += 4;
        uint32_t clear_b = read_le_u32(bytes + offset); offset += 4;
        (void)width;
        (void)height;
        json += String("{\"op\":\"clear\",\"r\":") + String(((double)clear_r) / 255.0)
             + String(",\"g\":") + String(((double)clear_g) / 255.0)
             + String(",\"b\":") + String(((double)clear_b) / 255.0)
             + String("}");
        emitted = 1;
    }

    while (offset + 4u <= (size_t)msg_len) {
        uint32_t opcode = read_le_u32(bytes + offset);
        offset += 4;
        if (opcode == 255u) {
            break;
        }
        if (opcode == 1u) {
            if (offset + 28u > (size_t)msg_len) break;
            uint32_t x = read_le_u32(bytes + offset); offset += 4;
            uint32_t y = read_le_u32(bytes + offset); offset += 4;
            uint32_t w = read_le_u32(bytes + offset); offset += 4;
            uint32_t h = read_le_u32(bytes + offset); offset += 4;
            uint32_t r = read_le_u32(bytes + offset); offset += 4;
            uint32_t g = read_le_u32(bytes + offset); offset += 4;
            uint32_t b = read_le_u32(bytes + offset); offset += 4;
            if (emitted) json += String(",");
            json += String("{\"op\":\"rect\",\"x\":") + String((int)x)
                 + String(",\"y\":") + String((int)y)
                 + String(",\"w\":") + String((int)w)
                 + String(",\"h\":") + String((int)h)
                 + String(",\"r\":") + String(((double)r) / 255.0)
                 + String(",\"g\":") + String(((double)g) / 255.0)
                 + String(",\"b\":") + String(((double)b) / 255.0)
                 + String("}");
            emitted = 1;
            continue;
        }
        if (opcode == 2u) {
            if (offset + 32u > (size_t)msg_len) break;
            uint32_t x0 = read_le_u32(bytes + offset); offset += 4;
            uint32_t y0 = read_le_u32(bytes + offset); offset += 4;
            uint32_t x1 = read_le_u32(bytes + offset); offset += 4;
            uint32_t y1 = read_le_u32(bytes + offset); offset += 4;
            uint32_t line_width = read_le_u32(bytes + offset); offset += 4;
            uint32_t r = read_le_u32(bytes + offset); offset += 4;
            uint32_t g = read_le_u32(bytes + offset); offset += 4;
            uint32_t b = read_le_u32(bytes + offset); offset += 4;
            if (emitted) json += String(",");
            json += String("{\"op\":\"line\",\"x0\":") + String((int)x0)
                 + String(",\"y0\":") + String((int)y0)
                 + String(",\"x1\":") + String((int)x1)
                 + String(",\"y1\":") + String((int)y1)
                 + String(",\"line_width\":") + String((int)line_width)
                 + String(",\"r\":") + String(((double)r) / 255.0)
                 + String(",\"g\":") + String(((double)g) / 255.0)
                 + String(",\"b\":") + String(((double)b) / 255.0)
                 + String("}");
            emitted = 1;
            continue;
        }
        break;
    }

    {
        SceneViewState scene = readSceneViewState(epa);
        if (emitted) {
            json += String(",");
        }
        json += String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Orange Exterminator\",\"size\":32,\"r\":1.0,\"g\":0.95,\"b\":0.90},");
        json += String("{\"op\":\"text\",\"x\":56,\"y\":82,\"text\":\"EPA artifact active\",\"size\":17,\"r\":0.84,\"g\":0.90,\"b\":0.76},");
        json += String("{\"op\":\"text\",\"x\":56,\"y\":110,\"text\":")
             + JsonString(String("lane=") + String(scene.lane) + String(" depth=") + String(scene.depth), true).toString()
             + String(",\"size\":17,\"r\":0.82,\"g\":0.86,\"b\":0.90}");
    }
    json += String("]");
    {
        Mutex::Lock lock(render_lock);
        latest_surface_commands = json;
        latest_surface_valid = emitted;
    }
    printf("surface mailbox parsed: emitted=%d\n", emitted);
    traceLine(String("{\"event\":\"mailbox_parsed\",\"emitted\":") + String(emitted) + String("}"));
    traceKernelStateSnapshot("after_mailbox");
}

String OrangeExterminatorApp::buildSurfaceCommandsJson() const {
    Mutex::Lock lock(render_lock);
    if (latest_surface_valid) {
        return latest_surface_commands;
    }
    {
        SceneViewState scene = readSceneViewState(epa);
        int lane = scene.lane;
        int depth = scene.depth;
        int spread = depth * 90;
        int inner = depth * 54;
        int center_x = 640 + lane;
        int dead_x = 535 + lane - (spread / 2);
        int dead_y = 210 - (depth * 26);
        int dead_w = 210 + (depth * 150);
        int dead_h = 170 + (depth * 58);
        int glow_x = 590 + lane - (depth * 40);
        int glow_y = 248 - (depth * 22);
        int glow_w = 100 + (depth * 78);
        int glow_h = 72 + (depth * 36);
        int wall_outer = 210 + (depth * 18);
        int wall_inner = 340 + (depth * 24);
        int far_y = 330 - (depth * 18);
        int mid_y = 410 - (depth * 12);

        return String("[")
            + String("{\"op\":\"clear\",\"r\":0.05,\"g\":0.05,\"b\":0.07},")
            + String("{\"op\":\"rect\",\"x\":0,\"y\":0,\"w\":1280,\"h\":250,\"r\":0.09,\"g\":0.11,\"b\":0.13},")
            + String("{\"op\":\"rect\",\"x\":0,\"y\":250,\"w\":1280,\"h\":120,\"r\":0.20,\"g\":0.22,\"b\":0.24},")
            + String("{\"op\":\"rect\",\"x\":0,\"y\":370,\"w\":1280,\"h\":350,\"r\":0.36,\"g\":0.28,\"b\":0.18},")
            + String("{\"op\":\"rect\",\"x\":0,\"y\":250,\"w\":210,\"h\":470,\"r\":0.10,\"g\":0.11,\"b\":0.13},")
            + String("{\"op\":\"rect\",\"x\":1070,\"y\":250,\"w\":210,\"h\":470,\"r\":0.10,\"g\":0.11,\"b\":0.13},")
            + String("{\"op\":\"rect\",\"x\":") + String(dead_x)
            + String(",\"y\":") + String(dead_y)
            + String(",\"w\":") + String(dead_w)
            + String(",\"h\":") + String(dead_h)
            + String(",\"r\":0.27,\"g\":0.29,\"b\":0.31},")
            + String("{\"op\":\"rect\",\"x\":") + String(glow_x)
            + String(",\"y\":") + String(glow_y)
            + String(",\"w\":") + String(glow_w)
            + String(",\"h\":") + String(glow_h)
            + String(",\"r\":0.67,\"g\":0.57,\"b\":0.42},")
            + String("{\"op\":\"text\",\"x\":54,\"y\":52,\"text\":\"Orange Exterminator\",\"size\":32,\"r\":1.0,\"g\":0.95,\"b\":0.90},")
            + String("{\"op\":\"text\",\"x\":56,\"y\":82,\"text\":\"Waiting for first committed EPA frame artifact. Live fallback scene active.\",\"size\":17,\"r\":0.95,\"g\":0.90,\"b\":0.82},")
            + String("{\"op\":\"text\",\"x\":56,\"y\":110,\"text\":") + JsonString(String("lane=") + String(lane) + String(" depth=") + String(depth), true).toString() + String(",\"size\":17,\"r\":0.82,\"g\":0.86,\"b\":0.90},")
            + String("{\"op\":\"line\",\"x0\":") + String(wall_outer + lane)
            + String(",\"y0\":720,\"x1\":") + String(560 + lane - spread)
            + String(",\"y1\":") + String(far_y)
            + String(",\"line_width\":3,\"r\":0.46,\"g\":0.40,\"b\":0.31},")
            + String("{\"op\":\"line\",\"x0\":") + String(1280 - wall_outer + lane)
            + String(",\"y0\":720,\"x1\":") + String(720 + lane + spread)
            + String(",\"y1\":") + String(far_y)
            + String(",\"line_width\":3,\"r\":0.46,\"g\":0.40,\"b\":0.31},")
            + String("{\"op\":\"line\",\"x0\":") + String(wall_inner + lane)
            + String(",\"y0\":720,\"x1\":") + String(595 + lane - inner)
            + String(",\"y1\":370,\"line_width\":2,\"r\":0.59,\"g\":0.53,\"b\":0.41},")
            + String("{\"op\":\"line\",\"x0\":") + String(1280 - wall_inner + lane)
            + String(",\"y0\":720,\"x1\":") + String(685 + lane + inner)
            + String(",\"y1\":370,\"line_width\":2,\"r\":0.59,\"g\":0.53,\"b\":0.41},")
            + String("{\"op\":\"line\",\"x0\":") + String(470 + lane)
            + String(",\"y0\":720,\"x1\":") + String(625 + lane - inner)
            + String(",\"y1\":") + String(mid_y)
            + String(",\"line_width\":2,\"r\":0.73,\"g\":0.67,\"b\":0.54},")
            + String("{\"op\":\"line\",\"x0\":") + String(810 + lane)
            + String(",\"y0\":720,\"x1\":") + String(655 + lane + inner)
            + String(",\"y1\":") + String(mid_y)
            + String(",\"line_width\":2,\"r\":0.73,\"g\":0.67,\"b\":0.54},")
            + String("{\"op\":\"line\",\"x0\":") + String(center_x)
            + String(",\"y0\":200,\"x1\":") + String(center_x)
            + String(",\"y1\":650,\"line_width\":1,\"r\":0.49,\"g\":0.51,\"b\":0.53},")
            + String("{\"op\":\"line\",\"x0\":0,\"y0\":370,\"x1\":1280,\"y1\":370,\"line_width\":2,\"r\":0.52,\"g\":0.45,\"b\":0.33},")
            + String("{\"op\":\"line\",\"x0\":0,\"y0\":250,\"x1\":1280,\"y1\":250,\"line_width\":1,\"r\":0.34,\"g\":0.35,\"b\":0.38},")
            + String("{\"op\":\"rect\",\"x\":592,\"y\":654,\"w\":96,\"h\":28,\"r\":0.14,\"g\":0.15,\"b\":0.16},")
            + String("{\"op\":\"rect\",\"x\":610,\"y\":662,\"w\":60,\"h\":12,\"r\":0.46,\"g\":0.48,\"b\":0.49},")
            + String("]");
    }
}

String OrangeExterminatorApp::buildStatusItemsJson() const {
    String bundle_state = bundle_exists
        ? String("EPA bundle found")
        : String("EPA bundle missing");

    String bundle_label = bundle_exists
        ? String("Bundle: ") + bundle_path
        : String("Bundle path pending build: ") + bundle_path;

    String module_state = epa_loaded
        ? String("EPA module loaded")
        : String("EPA module not loaded");

    String started_state = epa_started
        ? String("EPA kernels started")
        : String("EPA kernels not started");

    String kernel_count_label = String("Kernel count: ") + String((int)epa.kernelCount());
    SceneViewState scene = readSceneViewState(epa);
    String scene_label = String("Scene locals: lane=") + String(scene.lane)
        + String(" depth=") + String(scene.depth);

    return String("[")
        + String("{\"id\":\"ui\",\"label\":\"Vulkan surface shell online\"},")
        + String("{\"id\":\"bundle_state\",\"label\":") + JsonString(bundle_state, true).toString() + String("},")
        + String("{\"id\":\"bundle_path\",\"label\":") + JsonString(bundle_label, true).toString() + String("},")
        + String("{\"id\":\"module_state\",\"label\":") + JsonString(module_state, true).toString() + String("},")
        + String("{\"id\":\"started_state\",\"label\":") + JsonString(started_state, true).toString() + String("},")
        + String("{\"id\":\"kernel_count\",\"label\":") + JsonString(kernel_count_label, true).toString() + String("},")
        + String("{\"id\":\"scene\",\"label\":") + JsonString(scene_label, true).toString() + String("},")
        + String("{\"id\":\"next\",\"label\":\"Next gap: tighten the first native EPA frame artifact flow and remove fallback rendering\"}")
        + String("]");
}

void OrangeExterminatorApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    refreshProjectState();

    ui.clear();
    ui.createWindow(String("Orange Exterminator"), 1480, 920, String("org.elara.ui.orange-exterminator"));
    ui.setThemeMode(String("dark"));
    ui.setRootContent(String("app.shell"));

    ui.createGrid(String("app.shell"));
    ui.addGridColumnFill(String("app.shell"));
    ui.addGridColumnExact(String("app.shell"), 300);
    ui.setGridColumnBorderResizable(String("app.shell"), 0, true);
    ui.addGridRowFill(String("app.shell"));

    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("backend"), String("vulkan"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("orange.root.compose"));
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("Orange Exterminator"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setSectionJson(String("app.surface"), String("commands"), buildSurfaceCommandsJson());

    ui.createGrid(String("app.side"));
    ui.addGridColumnFill(String("app.side"));
    ui.addGridRowExact(String("app.side"), 34);
    ui.addGridRowExact(String("app.side"), 70);
    ui.addGridRowExact(String("app.side"), 40);
    ui.addGridRowExact(String("app.side"), 44);
    ui.addGridRowFill(String("app.side"));

    ui.createLabel(String("app.title"), String("Orange Exterminator"), 22);
    ui.createRichTextEdit(
        String("app.notes"),
        String("Movement slice online.\n")
        + String("The Vulkan surface is showing a simple third-person test scene.\n")
        + String("Arrow keys are routed into the EPA scene kernel.\n")
        + String("Up/down moves the camera toward the corridor dead end.\n")
        + String("Left/right shifts the corridor framing from the live scene worker locals.")
    );
    ui.setPropertyBool(String("app.notes"), String("read_only"), true);

    ui.createButton(String("app.reload"), String("Reload Surface"), String("app.reload"));
    ui.createButton(String("app.snapshot"), String("Snapshot"), String("app.snapshot"));

    ui.createListView(String("app.status"));
    ui.setSectionJson(String("app.status"), String("items"), buildStatusItemsJson());
    ui.setPropertyNumber(String("app.status"), String("font_size"), 14);

    ui.placeGridChild(String("app.side"), String("app.title"), 0, 0);
    ui.placeGridChild(String("app.side"), String("app.notes"), 0, 1);
    ui.placeGridChild(String("app.side"), String("app.reload"), 0, 2);
    ui.placeGridChild(String("app.side"), String("app.snapshot"), 0, 3);
    ui.placeGridChild(String("app.side"), String("app.status"), 0, 4);

    ui.placeGridChild(String("app.shell"), String("app.surface"), 0, 0);
    ui.placeGridChild(String("app.shell"), String("app.side"), 1, 0);
}

bool OrangeExterminatorApp::loadDocument(const String &document_json) {
    String params = String("{\"document\":") + JsonString(document_json, true).toString() + String("}");
    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(String("ui.loadDocument"), params, result_json, error_code, error_message, 5000)) {
        printf("ui.loadDocument failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        return false;
    }
    printf("Document loaded: %s\n", result_json.operator char *());
    return true;
}

bool OrangeExterminatorApp::setSectionJson(const String &target, const String &section, const String &value_json) {
    String params = String("{\"target\":")
        + JsonString(target, true).toString()
        + String(",\"section\":")
        + JsonString(section, true).toString()
        + String(",\"value\":")
        + value_json
        + String("}");
    String result_json;
    String error_code;
    String error_message;
    if(!peer->call(String("ui.setSectionJson"), params, result_json, error_code, error_message, 5000)) {
        String target_copy(target);
        String section_copy(section);
        printf("ui.setSectionJson failed [%s] target=%s section=%s: %s\n",
               error_code.operator char *(),
               target_copy.operator char *(),
               section_copy.operator char *(),
               error_message.operator char *());
        return false;
    }
    return true;
}

bool OrangeExterminatorApp::pushUiState() {
    if(incremental_ui_supported) {
        if(setSectionJson(String("app.surface"), String("commands"), buildSurfaceCommandsJson())) {
            if(setSectionJson(String("app.status"), String("items"), buildStatusItemsJson())) {
                return true;
            }
        }
        incremental_ui_supported = false;
        traceLine(String("{\"event\":\"ui_incremental_disabled\"}"));
    }

    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if(!loadDocument(ui.toJson())) {
        return false;
    }
    return true;
}

bool OrangeExterminatorApp::printSnapshot() {
    String result_json;
    String error_code;
    String error_message;
    if (peer->call(String("ui.snapshot"), String("{}"), result_json, error_code, error_message, 5000)) {
        printf("%s\n", result_json.operator char *());
        return true;
    }
    printf("ui.snapshot failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
    return false;
}

int OrangeExterminatorApp::run() {
    openTraceArtifact();

    peer->addService(Ref<sockets::rpc::json::JsonRPCService>(new UiEventSinkService(this)));

    if (!peer->connect(host, (unsigned short)port)) {
        printf("Failed to connect to %s:%d\n", host.operator char *(), port);
        closeTraceArtifact();
        return 1;
    }

    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        peer->close();
        epa.stopAllKernels();
        closeTraceArtifact();
        return 1;
    }
    armUiInputFocus();
    traceLine(String("{\"event\":\"ui_connected\",\"host\":") + JsonString(host, true).toString()
        + String(",\"port\":") + String(port) + String("}"));

    refreshEpaState();
    stimulateEpa();
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        g_orange_exterminator_app = NULL;
        epa.stopAllKernels();
        peer->close();
        closeTraceArtifact();
        return 1;
    }
    armUiInputFocus();

    int loop_count = 0;
    while (true) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 33000;

        int rc = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (rc > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[256];
            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }
            String command(line);
            command = command.trim();
            if (command == String("quit") || command == String("exit")) {
                break;
            }
            if (command == String("reload")) {
                refreshEpaState();
                stimulateEpa();
                if(!pushUiState()) {
                    break;
                }
                traceLine(String("{\"event\":\"manual_reload\"}"));
                continue;
            }
            if (command == String("snapshot")) {
                printSnapshot();
                traceLine(String("{\"event\":\"manual_snapshot\"}"));
                continue;
            }
            printf("Unhandled command: %s\n", command.operator char *());
        }

        drainKeyEvents();
        if(!pushUiState()) {
            break;
        }
        loop_count++;
        if ((loop_count % 15) == 0) {
            traceKernelStateSnapshot("loop");
        }
    }

    g_orange_exterminator_app = NULL;
    peer->close();
    epa.stopAllKernels();
    closeTraceArtifact();
    return 0;
}

}

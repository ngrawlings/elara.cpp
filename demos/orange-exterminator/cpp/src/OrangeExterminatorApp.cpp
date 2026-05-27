#include "OrangeExterminatorApp.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <math.h>
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

static int32_t read_le_i32(const unsigned char *p) {
    return (int32_t)read_le_u32(p);
}

static double parse_json_number_after(const String& text, const String& needle, double fallback) {
    String text_copy(text);
    int index = text_copy.indexOf(needle);
    if(index < 0) {
        return fallback;
    }
    String fragment = text_copy.substr(index + needle.length()).trim();
    return strtod(fragment.operator char *(), NULL);
}

static int parse_json_int_after(const String& text, const String& needle, int fallback) {
    String text_copy(text);
    int index = text_copy.indexOf(needle);
    if(index < 0) {
        return fallback;
    }
    String fragment = text_copy.substr(index + needle.length()).trim();
    return (int)strtol(fragment.operator char *(), NULL, 10);
}

struct SceneViewState {
    int cam_x;
    int cam_y;
    int cam_z;
    int cam_yaw;
    int cam_pitch;
    int depth;
    int lane;
};

struct SceneMarkerState {
    bool visible;
    int x;
    int y;
    double depth;
};

struct ProjectedRectState {
    bool visible;
    int x;
    int y;
    int w;
    int h;
};

struct Scene3DCamera {
    double x;
    double y;
    double z;
    double yaw_deg;
    double pitch_deg;
    double roll_deg;
    double fov_deg;
    double near_z;
    double far_z;
};

struct Scene3DInstance {
    int id;
    int mesh_id;
    int material_id;
    double x;
    double y;
    double z;
    double yaw_deg;
    double pitch_deg;
    double roll_deg;
    double sx;
    double sy;
    double sz;
    double r;
    double g;
    double b;
};

struct ScenePoint2D {
    bool visible;
    double x;
    double y;
    double z;
};

struct CalibrationProjectionState {
    ProjectedRectState end_wall;
    ProjectedRectState side_wall;
    SceneMarkerState marker0;
    SceneMarkerState marker1;
    SceneMarkerState marker2;
};

static int clampInt(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int wrapDegrees360(int value) {
    int wrapped = value % 360;
    if (wrapped < 0) {
        wrapped += 360;
    }
    return wrapped;
}

static double sceneMilli(int value) {
    return ((double)value) / 1000.0;
}

static double sceneMilliDeg(int value) {
    return ((double)value) / 1000.0;
}

static String jsonLineCommand(double x0, double y0, double x1, double y1, double width,
                              double r, double g, double b) {
    return String("{\"op\":\"line\",\"x0\":") + String(x0)
        + String(",\"y0\":") + String(y0)
        + String(",\"x1\":") + String(x1)
        + String(",\"y1\":") + String(y1)
        + String(",\"line_width\":") + String(width)
        + String(",\"r\":") + String(r)
        + String(",\"g\":") + String(g)
        + String(",\"b\":") + String(b)
        + String("}");
}

static void appendJsonCommand(String &json, int &emitted, const String &command) {
    if (emitted) {
        json += String(",");
    }
    json += command;
    emitted = 1;
}

static ScenePoint2D projectScenePoint(const Scene3DCamera &cam, double wx, double wy, double wz,
                                      int width, int height) {
    double dx = wx - cam.x;
    double dy = wy - cam.y;
    double dz = wz - cam.z;
    double yaw = -cam.yaw_deg * 3.14159265358979323846 / 180.0;
    double pitch = -cam.pitch_deg * 3.14159265358979323846 / 180.0;
    double cy = cos(yaw);
    double sy = sin(yaw);
    double cp = cos(pitch);
    double sp = sin(pitch);

    double vx = (dx * cy) - (dz * sy);
    double vz = (dx * sy) + (dz * cy);
    double vy = (dy * cp) - (vz * sp);
    vz = (dy * sp) + (vz * cp);

    ScenePoint2D out;
    out.visible = false;
    out.x = 0.0;
    out.y = 0.0;
    out.z = vz;
    if (vz <= cam.near_z || vz >= cam.far_z) {
        return out;
    }

    double fov = cam.fov_deg > 1.0 ? cam.fov_deg : 60.0;
    double focal = ((double)height * 0.5) / tan((fov * 3.14159265358979323846 / 180.0) * 0.5);
    out.x = ((double)width * 0.5) + ((vx / vz) * focal);
    out.y = ((double)height * 0.5) - ((vy / vz) * focal);
    out.visible = true;
    return out;
}

static void appendSceneInstanceWireframe(String &json, int &emitted, const Scene3DCamera &cam,
                                         const Scene3DInstance &inst, int width, int height) {
    double hx = inst.sx * 0.5;
    double hy = inst.sy * 0.5;
    double hz = inst.sz * 0.5;
    double yaw = inst.yaw_deg * 3.14159265358979323846 / 180.0;
    double cy = cos(yaw);
    double sy = sin(yaw);
    double corners[8][3] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz},
    };
    ScenePoint2D pts[8];
    for (int i = 0; i < 8; i++) {
        double lx = corners[i][0];
        double ly = corners[i][1];
        double lz = corners[i][2];
        double wx = inst.x + (lx * cy) - (lz * sy);
        double wz = inst.z + (lx * sy) + (lz * cy);
        double wy = inst.y + ly;
        pts[i] = projectScenePoint(cam, wx, wy, wz, width, height);
    }
    const int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    for (int i = 0; i < 12; i++) {
        const ScenePoint2D &a = pts[edges[i][0]];
        const ScenePoint2D &b = pts[edges[i][1]];
        if (!a.visible || !b.visible) {
            continue;
        }
        appendJsonCommand(json, emitted, jsonLineCommand(a.x, a.y, b.x, b.y, 2.0, inst.r, inst.g, inst.b));
    }
}

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
                    app->updateKeyState(keyval, true);
                }
            } else if (app && params.indexOf(String("\"action\":\"keyUp\"")) >= 0) {
                int key_index = params.indexOf(String("\"keyval\":"));
                if (key_index >= 0) {
                    String fragment = params.substr(key_index + 9).trim();
                    unsigned int keyval = (unsigned int)strtoul(fragment.operator char *(), NULL, 10);
                    app->updateKeyState(keyval, false);
                }
            } else if (app && params.indexOf(String("\"action\":\"mouseDown\"")) >= 0) {
                int button = parse_json_int_after(params, String("\"button\":"), 0);
                double x = parse_json_number_after(params, String("\"x\":"), 0.0);
                double y = parse_json_number_after(params, String("\"y\":"), 0.0);
                app->handleMouseDown(button, x, y);
            } else if (app && params.indexOf(String("\"action\":\"mouseUp\"")) >= 0) {
                int button = parse_json_int_after(params, String("\"button\":"), 0);
                double x = parse_json_number_after(params, String("\"x\":"), 0.0);
                double y = parse_json_number_after(params, String("\"y\":"), 0.0);
                app->handleMouseUp(button, x, y);
            } else if (app && params.indexOf(String("\"action\":\"mouseMove\"")) >= 0) {
                double x = parse_json_number_after(params, String("\"x\":"), 0.0);
                double y = parse_json_number_after(params, String("\"y\":"), 0.0);
                app->handleMouseMove(x, y);
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

SceneViewState clampSceneViewState(const SceneViewState &input) {
    SceneViewState state = input;
    state.cam_x = clampInt(state.cam_x, -4096, 4096);
    state.cam_y = clampInt(state.cam_y, -1024, 1024);
    state.cam_z = clampInt(state.cam_z, -4096, 4096);
    state.cam_yaw = wrapDegrees360(state.cam_yaw);
    state.cam_pitch = clampInt(state.cam_pitch, -89, 89);
    state.depth = clampInt(state.depth, 0, 6);
    state.lane = clampInt(state.lane, -320, 320);
    return state;
}

static SceneMarkerState projectWorldPoint(const SceneViewState &input, double wx, double wy, double wz) {
    SceneViewState scene = clampSceneViewState(input);
    SceneMarkerState marker;
    double yaw = ((double)scene.cam_yaw) * 3.14159265358979323846 / 180.0;
    double pitch = ((double)scene.cam_pitch) * 3.14159265358979323846 / 180.0;
    double dx = wx - (double)scene.cam_x;
    double dy = wy - (double)scene.cam_y;
    double dz = wz - (double)scene.cam_z;
    double cy = cos(yaw);
    double sy = sin(yaw);
    double cp = cos(pitch);
    double sp = sin(pitch);
    double view_x = (cy * dx) - (sy * dz);
    double view_z = (sy * dx) + (cy * dz);
    double view_y = (cp * dy) - (sp * view_z);
    double depth_z = (sp * dy) + (cp * view_z);
    double focal = 760.0;
    marker.visible = false;
    marker.x = 0;
    marker.y = 0;
    marker.depth = depth_z;
    if (depth_z <= 32.0) {
        return marker;
    }
    marker.x = (int)lrint(640.0 + ((view_x * focal) / depth_z));
    marker.y = (int)lrint(360.0 - ((view_y * focal) / depth_z));
    marker.visible = true;
    return marker;
}

static ProjectedRectState projectWallRect(
    const SceneViewState &input,
    double ax, double ay, double az,
    double bx, double by, double bz,
    double cx, double cy, double cz,
    double dx, double dy, double dz
) {
    SceneMarkerState p0 = projectWorldPoint(input, ax, ay, az);
    SceneMarkerState p1 = projectWorldPoint(input, bx, by, bz);
    SceneMarkerState p2 = projectWorldPoint(input, cx, cy, cz);
    SceneMarkerState p3 = projectWorldPoint(input, dx, dy, dz);
    ProjectedRectState rect;
    rect.visible = false;
    rect.x = 0;
    rect.y = 0;
    rect.w = 0;
    rect.h = 0;
    if (!p0.visible || !p1.visible || !p2.visible || !p3.visible) {
        return rect;
    }
    int min_x = p0.x;
    int max_x = p0.x;
    int min_y = p0.y;
    int max_y = p0.y;
    if (p1.x < min_x) min_x = p1.x;
    if (p2.x < min_x) min_x = p2.x;
    if (p3.x < min_x) min_x = p3.x;
    if (p1.x > max_x) max_x = p1.x;
    if (p2.x > max_x) max_x = p2.x;
    if (p3.x > max_x) max_x = p3.x;
    if (p1.y < min_y) min_y = p1.y;
    if (p2.y < min_y) min_y = p2.y;
    if (p3.y < min_y) min_y = p3.y;
    if (p1.y > max_y) max_y = p1.y;
    if (p2.y > max_y) max_y = p2.y;
    if (p3.y > max_y) max_y = p3.y;
    rect.x = min_x;
    rect.y = min_y;
    rect.w = max_x - min_x;
    rect.h = max_y - min_y;
    rect.visible = (rect.w > 2 && rect.h > 2);
    return rect;
}

static CalibrationProjectionState projectCalibrationScene(const SceneViewState &input) {
    CalibrationProjectionState projection;
    (void)input;
    memset(&projection, 0, sizeof(projection));
    return projection;
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
      last_section_update_timed_out(false),
      input_lock("orange-exterminator-input"),
      render_lock("orange-exterminator-render"),
      held_forward(false),
      held_back(false),
      held_left(false),
      held_right(false),
      pending_mouse_dx(0),
      pending_mouse_dy(0),
      mouse_captured(false),
      mouse_capture_requested(false),
      mouse_uncapture_requested(false),
      scene_cam_x(0),
      scene_cam_y(0),
      scene_cam_z(0),
      scene_cam_yaw(0),
      scene_cam_pitch(0),
      scene_depth(0),
      scene_lane(0),
      latest_surface_valid(false),
      surface_revision(0),
      pushed_surface_revision(0),
      trace_path(String("..") + String("/") + String("artifacts") + String("/") + String("live-epa-trace.jsonl")),
      trace_file(NULL),
      trace_sequence(0),
      peer(new ElaraUiRpcPeer()) {
}

OrangeExterminatorApp::~OrangeExterminatorApp() {
    shutdown();
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
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseMove\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseDown\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseUp\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.setFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.lockFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);

    traceLine(String("{\"event\":\"ui_input_focus_armed\",\"target\":\"app.surface\"}"));
}

void OrangeExterminatorApp::armMouseCapture() {
    setMouseCaptured(false);
    traceLine(String("{\"event\":\"mouse_capture_armed_click_surface\"}"));
}

void OrangeExterminatorApp::setMouseCaptured(bool captured) {
    String result_json;
    String error_code;
    String error_message;
    mouse_captured = captured;
    mouse_capture_requested = false;
    mouse_uncapture_requested = false;
    {
        Mutex::Lock lock(input_lock);
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
    }
    bool ok = peer->call(
        String("ui.setMouseCaptured"),
        String("{\"captured\":") + String(captured ? "true" : "false") + String("}"),
        result_json,
        error_code,
        error_message,
        5000
    );
    if (!ok) {
        printf("ui.setMouseCaptured failed [%s]: %s\n",
               error_code.operator char *(),
               error_message.operator char *());
    } else {
        printf("ui.setMouseCaptured ok: captured=%d\n", captured ? 1 : 0);
    }
    if (captured) {
        peer->call(String("ui.setFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);
        peer->call(String("ui.lockFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);
    }
    traceLine(String("{\"event\":\"mouse_capture_state\",\"captured\":") + String(captured ? "true" : "false") + String("}"));
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
    scene_cam_x = 0;
    scene_cam_y = 0;
    scene_cam_z = 0;
    scene_cam_yaw = 0;
    scene_cam_pitch = 0;
    scene_depth = 0;
    scene_lane = 0;
    epa.destroy();
    {
        Mutex::Lock lock(render_lock);
        latest_surface_valid = false;
        latest_surface_commands = String();
        surface_revision = 0;
        pushed_surface_revision = 0;
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
        struct ScenePoseInputPayload {
            int32_t cam_x; int32_t cam_z; int32_t depth; int32_t lane; int32_t yaw; int32_t pitch;
            int32_t end_wall_x; int32_t end_wall_y; int32_t end_wall_w; int32_t end_wall_h; int32_t end_wall_visible;
            int32_t side_wall_x; int32_t side_wall_y; int32_t side_wall_w; int32_t side_wall_h; int32_t side_wall_visible;
            int32_t marker0_x; int32_t marker0_y; int32_t marker0_visible;
            int32_t marker1_x; int32_t marker1_y; int32_t marker1_visible;
            int32_t marker2_x; int32_t marker2_y; int32_t marker2_visible;
        };
        CalibrationProjectionState projection = projectCalibrationScene(
            SceneViewState{ scene_cam_x, scene_cam_y, scene_cam_z, scene_cam_yaw, scene_cam_pitch, scene_depth, scene_lane }
        );
        ScenePoseInputPayload pose_init = {
            scene_cam_x, scene_cam_z, scene_depth, scene_lane, wrapDegrees360(scene_cam_yaw), clampInt(scene_cam_pitch, -89, 89),
            projection.end_wall.x, projection.end_wall.y, projection.end_wall.w, projection.end_wall.h, projection.end_wall.visible ? 1 : 0,
            projection.side_wall.x, projection.side_wall.y, projection.side_wall.w, projection.side_wall.h, projection.side_wall.visible ? 1 : 0,
            projection.marker0.x, projection.marker0.y, projection.marker0.visible ? 1 : 0,
            projection.marker1.x, projection.marker1.y, projection.marker1.visible ? 1 : 0,
            projection.marker2.x, projection.marker2.y, projection.marker2.visible ? 1 : 0
        };
        int ok1 = epa.ingressPushToKernel((size_t)idx, 1u, &pose_init, sizeof(pose_init)) ? 1 : 0;
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

bool OrangeExterminatorApp::sendScenePose() {
    struct ScenePoseInputPayload {
        int32_t cam_x;
        int32_t cam_z;
        int32_t depth;
        int32_t lane;
        int32_t yaw;
        int32_t pitch;
        int32_t end_wall_x;
        int32_t end_wall_y;
        int32_t end_wall_w;
        int32_t end_wall_h;
        int32_t end_wall_visible;
        int32_t side_wall_x;
        int32_t side_wall_y;
        int32_t side_wall_w;
        int32_t side_wall_h;
        int32_t side_wall_visible;
        int32_t marker0_x;
        int32_t marker0_y;
        int32_t marker0_visible;
        int32_t marker1_x;
        int32_t marker1_y;
        int32_t marker1_visible;
        int32_t marker2_x;
        int32_t marker2_y;
        int32_t marker2_visible;
    };

    if (!epa_started) {
        return false;
    }

    int idx = epa.findKernelIndex(String("scene"));
    if (idx < 0) {
        return false;
    }

    CalibrationProjectionState projection = projectCalibrationScene(
        SceneViewState{ scene_cam_x, scene_cam_y, scene_cam_z, scene_cam_yaw, scene_cam_pitch, scene_depth, scene_lane }
    );
    ScenePoseInputPayload pose = {
        scene_cam_x,
        scene_cam_z,
        scene_depth,
        scene_lane,
        wrapDegrees360(scene_cam_yaw),
        clampInt(scene_cam_pitch, -89, 89),
        projection.end_wall.x,
        projection.end_wall.y,
        projection.end_wall.w,
        projection.end_wall.h,
        projection.end_wall.visible ? 1 : 0,
        projection.side_wall.x,
        projection.side_wall.y,
        projection.side_wall.w,
        projection.side_wall.h,
        projection.side_wall.visible ? 1 : 0,
        projection.marker0.x,
        projection.marker0.y,
        projection.marker0.visible ? 1 : 0,
        projection.marker1.x,
        projection.marker1.y,
        projection.marker1.visible ? 1 : 0,
        projection.marker2.x,
        projection.marker2.y,
        projection.marker2.visible ? 1 : 0
    };
    printf("sendScenePose: cam_x=%d cam_z=%d depth=%d lane=%d yaw=%d pitch=%d "
           "m0=(%d,%d,%d) m1=(%d,%d,%d) m2=(%d,%d,%d) end=(%d,%d,%d,%d,%d)\n",
           pose.cam_x, pose.cam_z, pose.depth, pose.lane, pose.yaw, pose.pitch,
           pose.marker0_x, pose.marker0_y, pose.marker0_visible,
           pose.marker1_x, pose.marker1_y, pose.marker1_visible,
           pose.marker2_x, pose.marker2_y, pose.marker2_visible,
           pose.end_wall_x, pose.end_wall_y, pose.end_wall_w, pose.end_wall_h, pose.end_wall_visible);
    fflush(stdout);
    return epa.ingressPushToKernel((size_t)idx, 1u, &pose, sizeof(pose));
}

void OrangeExterminatorApp::enqueueKeyDown(unsigned int keyval) {
    Mutex::Lock lock(input_lock);
    pending_keydowns.push(keyval);
}

void OrangeExterminatorApp::updateKeyState(unsigned int keyval, bool pressed) {
    Mutex::Lock lock(input_lock);
    // Arrow keys and WASD both control movement.
    if (keyval == 65362u || keyval == 119u) { held_forward = pressed; }  // Up / W
    if (keyval == 65364u || keyval == 115u) { held_back    = pressed; }  // Down / S
    if (keyval == 65361u || keyval == 97u)  { held_left    = pressed; }  // Left / A
    if (keyval == 65363u || keyval == 100u) { held_right   = pressed; }  // Right / D
    if (keyval == 65307u && pressed) {
        mouse_uncapture_requested = true;
    }
    printf("updateKeyState: keyval=%u pressed=%d held_f=%d held_b=%d held_l=%d held_r=%d\n",
           keyval, (int)pressed, (int)held_forward, (int)held_back, (int)held_left, (int)held_right);
}

void OrangeExterminatorApp::accumulateMouseDelta(int dx, int dy) {
    Mutex::Lock lock(input_lock);
    if (dx > 64) { dx = 64; }
    if (dx < -64) { dx = -64; }
    if (dy > 64) { dy = 64; }
    if (dy < -64) { dy = -64; }
    if (dx > -2 && dx < 2) {
        dx = 0;
    }
    if (dy > -2 && dy < 2) {
        dy = 0;
    }

    pending_mouse_dx += dx;
    pending_mouse_dy += dy;
    if (pending_mouse_dx > 128) { pending_mouse_dx = 128; }
    if (pending_mouse_dx < -128) { pending_mouse_dx = -128; }
    if (pending_mouse_dy > 128) { pending_mouse_dy = 128; }
    if (pending_mouse_dy < -128) { pending_mouse_dy = -128; }
}

void OrangeExterminatorApp::handleMouseDown(int button, double x, double y) {
    (void)x;
    (void)y;
    printf("handleMouseDown: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1) {
        mouse_capture_requested = true;
    }
}

void OrangeExterminatorApp::handleMouseUp(int button, double x, double y) {
    printf("handleMouseUp: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1 && !mouse_captured) {
        mouse_capture_requested = true;
    }
}

void OrangeExterminatorApp::handleMouseMove(double x, double y) {
    if (!mouse_captured) {
        return;
    }
    accumulateMouseDelta((int)x, (int)y);
}

void OrangeExterminatorApp::drainKeyEvents() {
    if (!epa_started) {
        return;
    }

    if (mouse_uncapture_requested) {
        setMouseCaptured(false);
    } else if (mouse_capture_requested && !mouse_captured) {
        setMouseCaptured(true);
    }

    int move_x = 0;
    int move_z = 0;
    int look_dx = 0;
    int look_dy = 0;
    {
        Mutex::Lock lock(input_lock);
        move_x = (held_right ? 1 : 0) - (held_left ? 1 : 0);
        move_z = (held_forward ? 1 : 0) - (held_back ? 1 : 0);
        look_dx = pending_mouse_dx;
        look_dy = pending_mouse_dy;
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
        pending_keydowns.clear();
    }

    if (move_x == 0 && move_z == 0 && look_dx == 0 && look_dy == 0) {
        return;
    }

    scene_cam_x += (move_x * 24);
    scene_cam_z += (move_z * 96);
    scene_lane += (move_x * 24);
    scene_cam_yaw = wrapDegrees360(scene_cam_yaw + (look_dx / 6));
    scene_cam_pitch = clampInt(scene_cam_pitch - (look_dy / 8), -89, 89);
    if (move_z > 0) {
        scene_depth = 1;
    } else if (move_z < 0) {
        scene_depth = 0;
    }
    scene_cam_x = clampInt(scene_cam_x, -4096, 4096);
    scene_cam_z = clampInt(scene_cam_z, -4096, 4096);
    scene_lane = clampInt(scene_lane, -320, 320);
    scene_depth = clampInt(scene_depth, 0, 1);

    printf("drainKeyEvents: cam_x=%d cam_z=%d depth=%d lane=%d yaw=%d pitch=%d\n",
           scene_cam_x, scene_cam_z, scene_depth, scene_lane, scene_cam_yaw, scene_cam_pitch);
    sendScenePose();
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

void OrangeExterminatorApp::shutdown() {
    g_orange_exterminator_app = NULL;

    int idx = epa.findKernelIndex(String("scene"));
    if (idx >= 0) {
        EpaKernel *kernel = epa.rawKernelAt((size_t)idx);
        if (kernel) {
            epa_kernel_set_signal_callback(kernel, NULL);
        }
    }

    if (peer) {
        peer->close();
        peer = Ref<ui::rpc::ElaraUiRpcPeer>(0);
    }

    epa.stopAllKernels();
    epa.destroy();
    closeTraceArtifact();
}

void OrangeExterminatorApp::updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len) {
    const unsigned char *bytes = (const unsigned char *)msg;
    size_t offset = 0;
    String json("[");
    String rect_summary("");
    int emitted = 0;
    int rect_count = 0;
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
    uint32_t width = read_le_u32(bytes + offset); offset += 4;
    uint32_t height = read_le_u32(bytes + offset); offset += 4;
    uint32_t frame_type = read_le_u32(bytes + offset); offset += 4;
    uint32_t frame_id = read_le_u32(bytes + offset); offset += 4;
    uint32_t record_count = read_le_u32(bytes + offset); offset += 4;

    if (frame_type == 3u) {
        Scene3DCamera camera;
        camera.x = 0.0;
        camera.y = 0.62;
        camera.z = -0.90;
        camera.yaw_deg = 0.0;
        camera.pitch_deg = 0.0;
        camera.roll_deg = 0.0;
        camera.fov_deg = 60.0;
        camera.near_z = 0.08;
        camera.far_z = 12.0;

        double clear_r = 18.0 / 255.0;
        double clear_g = 20.0 / 255.0;
        double clear_b = 24.0 / 255.0;
        double default_r = 1.0;
        double default_g = 0.52;
        double default_b = 0.08;
        double material_r[16];
        double material_g[16];
        double material_b[16];
        std::vector<Scene3DInstance> instances;
        for (int i = 0; i < 16; i++) {
            material_r[i] = default_r;
            material_g[i] = default_g;
            material_b[i] = default_b;
        }

        while (offset + 4u <= (size_t)msg_len) {
            uint32_t record_opcode = read_le_u32(bytes + offset);
            offset += 4;
            if (record_opcode == 255u) {
                break;
            }
            if (record_opcode != 2u || offset + 32u > (size_t)msg_len) {
                break;
            }
            int32_t op = read_le_i32(bytes + offset); offset += 4;
            int32_t a0 = read_le_i32(bytes + offset); offset += 4;
            int32_t a1 = read_le_i32(bytes + offset); offset += 4;
            int32_t a2 = read_le_i32(bytes + offset); offset += 4;
            int32_t a3 = read_le_i32(bytes + offset); offset += 4;
            int32_t a4 = read_le_i32(bytes + offset); offset += 4;
            int32_t a5 = read_le_i32(bytes + offset); offset += 4;
            int32_t a6 = read_le_i32(bytes + offset); offset += 4;

            if (op == 10) {
                camera.x = sceneMilli(a0);
                camera.y = sceneMilli(a1);
                camera.z = sceneMilli(a2);
                camera.yaw_deg = sceneMilliDeg(a3);
                camera.pitch_deg = sceneMilliDeg(a4);
                camera.roll_deg = sceneMilliDeg(a5);
                camera.fov_deg = sceneMilliDeg(a6);
            } else if (op == 11) {
                camera.near_z = sceneMilli(a0);
                camera.far_z = sceneMilli(a1);
                if (camera.near_z < 0.01) camera.near_z = 0.01;
                if (camera.far_z <= camera.near_z) camera.far_z = camera.near_z + 10.0;
            } else if (op == 20) {
                clear_r = ((double)clampInt(a0, 0, 255)) / 255.0;
                clear_g = ((double)clampInt(a1, 0, 255)) / 255.0;
                clear_b = ((double)clampInt(a2, 0, 255)) / 255.0;
            } else if (op == 30) {
                int mat = clampInt(a0, 0, 15);
                material_r[mat] = ((double)clampInt(a1, 0, 255)) / 255.0;
                material_g[mat] = ((double)clampInt(a2, 0, 255)) / 255.0;
                material_b[mat] = ((double)clampInt(a3, 0, 255)) / 255.0;
            } else if (op == 50) {
                Scene3DInstance inst;
                inst.id = a0;
                inst.mesh_id = a1;
                inst.material_id = clampInt(a2, 0, 15);
                inst.x = sceneMilli(a3);
                inst.y = sceneMilli(a4);
                inst.z = sceneMilli(a5);
                inst.yaw_deg = 0.0;
                inst.pitch_deg = 0.0;
                inst.roll_deg = 0.0;
                inst.sx = 0.35;
                inst.sy = 0.35;
                inst.sz = 0.35;
                inst.r = material_r[inst.material_id];
                inst.g = material_g[inst.material_id];
                inst.b = material_b[inst.material_id];
                instances.push_back(inst);
            } else if (op == 51) {
                for (size_t i = 0; i < instances.size(); i++) {
                    if (instances[i].id == a0) {
                        instances[i].yaw_deg = sceneMilliDeg(a1);
                        instances[i].pitch_deg = sceneMilliDeg(a2);
                        instances[i].roll_deg = sceneMilliDeg(a3);
                        instances[i].sx = sceneMilli(a4);
                        instances[i].sy = sceneMilli(a5);
                        instances[i].sz = sceneMilli(a6);
                    }
                }
            }
        }

        appendJsonCommand(json, emitted,
            String("{\"op\":\"clear\",\"r\":") + String(clear_r)
            + String(",\"g\":") + String(clear_g)
            + String(",\"b\":") + String(clear_b)
            + String("}"));
        for (size_t i = 0; i < instances.size(); i++) {
            appendSceneInstanceWireframe(json, emitted, camera, instances[i], (int)width, (int)height);
        }
        appendJsonCommand(json, emitted,
            String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Orange Exterminator 3D E3SB\",\"size\":30,\"r\":1.0,\"g\":0.95,\"b\":0.90}"));
        appendJsonCommand(json, emitted,
            String("{\"op\":\"text\",\"x\":56,\"y\":84,\"text\":")
            + JsonString(String("frame=") + String((int)frame_id)
                + String(" records=") + String((int)record_count)
                + String(" instances=") + String((int)instances.size()), true).toString()
            + String(",\"size\":17,\"r\":0.82,\"g\":0.90,\"b\":0.78}"));

        json += String("]");
        {
            Mutex::Lock lock(render_lock);
            latest_surface_commands = json;
            latest_surface_valid = emitted;
            if (emitted) {
                surface_revision++;
            }
        }
        printf("surface E3SB parsed: emitted=%d instances=%d frame=%u records=%u\n",
               emitted, (int)instances.size(), (unsigned)frame_id, (unsigned)record_count);
        traceLine(String("{\"event\":\"mailbox_e3sb_parsed\",\"emitted\":") + String(emitted)
            + String(",\"instances\":") + String((int)instances.size()) + String("}"));
        traceKernelStateSnapshot("after_mailbox_e3sb");
        return;
    }

    uint32_t clear_r = frame_type;
    uint32_t clear_g = frame_id;
    uint32_t clear_b = record_count;
    json += String("{\"op\":\"clear\",\"r\":") + String(((double)clear_r) / 255.0)
         + String(",\"g\":") + String(((double)clear_g) / 255.0)
         + String(",\"b\":") + String(((double)clear_b) / 255.0)
         + String("}");
    emitted = 1;

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
            if (rect_summary.length() > 0) {
                rect_summary += String(" ");
            }
            rect_summary += String("#") + String(rect_count)
                         + String("(") + String((int)x)
                         + String(",") + String((int)y)
                         + String(",") + String((int)w)
                         + String(",") + String((int)h)
                         + String(")");
            rect_count++;
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
        SceneViewState scene = clampSceneViewState(
            SceneViewState{ scene_cam_x, scene_cam_y, scene_cam_z, scene_cam_yaw, scene_cam_pitch, scene_depth, scene_lane }
        );
        if (emitted) {
            json += String(",");
        }
        json += String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Orange Exterminator\",\"size\":32,\"r\":1.0,\"g\":0.95,\"b\":0.90},");
        json += String("{\"op\":\"text\",\"x\":56,\"y\":82,\"text\":\"EPA artifact active\",\"size\":17,\"r\":0.84,\"g\":0.90,\"b\":0.76},");
        json += String("{\"op\":\"text\",\"x\":56,\"y\":110,\"text\":")
             + JsonString(String("x=") + String(scene.cam_x) + String(" y=") + String(scene.cam_y) + String(" z=") + String(scene.cam_z), true).toString()
             + String(",\"size\":17,\"r\":0.82,\"g\":0.86,\"b\":0.90},");
        json += String("{\"op\":\"text\",\"x\":56,\"y\":130,\"text\":")
             + JsonString(String("yaw=") + String(scene.cam_yaw) + String(" pitch=") + String(scene.cam_pitch), true).toString()
             + String(",\"size\":17,\"r\":0.82,\"g\":0.86,\"b\":0.90}");
    }
    json += String("]");
    {
        Mutex::Lock lock(render_lock);
        latest_surface_commands = json;
        latest_surface_valid = emitted;
        if (emitted) {
            surface_revision++;
        }
    }
    printf("surface mailbox parsed: emitted=%d\n", emitted);
    if (rect_summary.length() > 0) {
        printf("surface mailbox rects: %s\n", rect_summary.operator char *());
    }
    traceLine(String("{\"event\":\"mailbox_parsed\",\"emitted\":") + String(emitted) + String("}"));
    traceKernelStateSnapshot("after_mailbox");
}

String OrangeExterminatorApp::buildSurfaceCommandsJson() const {
    Mutex::Lock lock(render_lock);
    if (latest_surface_valid) {
        return latest_surface_commands;
    }
    return String("[]");
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
    SceneViewState scene = clampSceneViewState(
        SceneViewState{ scene_cam_x, scene_cam_y, scene_cam_z, scene_cam_yaw, scene_cam_pitch, scene_depth, scene_lane }
    );
    String scene_label = String("x=") + String(scene.cam_x)
        + String(" y=") + String(scene.cam_y)
        + String(" z=") + String(scene.cam_z)
        + String(" yaw=") + String(scene.cam_yaw)
        + String(" pitch=") + String(scene.cam_pitch)
        + String(" lane=") + String(scene.lane)
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
    ui.setRootContent(String("app.surface"));

    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("backend"), String("vulkan"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("orange.root.compose"));
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("Orange Exterminator"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setSectionJson(String("app.surface"), String("commands"), buildSurfaceCommandsJson());
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
    last_section_update_timed_out = false;
    if(!peer->call(String("ui.setSectionJson"), params, result_json, error_code, error_message, 5000)) {
        String target_copy(target);
        String section_copy(section);
        printf("ui.setSectionJson failed [%s] target=%s section=%s: %s\n",
               error_code.operator char *(),
               target_copy.operator char *(),
               section_copy.operator char *(),
               error_message.operator char *());
        if (error_code == String("timeout")) {
            last_section_update_timed_out = true;
        }
        return false;
    }
    printf("ui.setSectionJson ok: target=%s section=%s bytes=%d\n",
           String(target).operator char *(),
           String(section).operator char *(),
           value_json.length());
    return true;
}

bool OrangeExterminatorApp::pushUiState() {
    unsigned long current_revision = 0;
    unsigned long current_pushed_revision = 0;
    {
        Mutex::Lock lock(render_lock);
        current_revision = surface_revision;
        current_pushed_revision = pushed_surface_revision;
        if (surface_revision == pushed_surface_revision) {
            return true;
        }
    }
    printf("pushUiState: surface_revision=%lu pushed_surface_revision=%lu\n",
           current_revision,
           current_pushed_revision);

    if(incremental_ui_supported) {
        if(setSectionJson(String("app.surface"), String("commands"), buildSurfaceCommandsJson())) {
            Mutex::Lock lock(render_lock);
            pushed_surface_revision = surface_revision;
            printf("pushUiState applied: pushed_surface_revision=%lu\n", pushed_surface_revision);
            return true;
        }
        if (last_section_update_timed_out) {
            traceLine(String("{\"event\":\"ui_incremental_timeout_retry\"}"));
            return true;
        }
        traceLine(String("{\"event\":\"ui_incremental_failed_retry\"}"));
        return true;
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
        shutdown();
        return 1;
    }

    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        shutdown();
        return 1;
    }
    armUiInputFocus();
    traceLine(String("{\"event\":\"ui_connected\",\"host\":") + JsonString(host, true).toString()
        + String(",\"port\":") + String(port) + String("}"));

    refreshEpaState();
    stimulateEpa();
    buildDocument(ui);
    if (!loadDocument(ui.toJson())) {
        shutdown();
        return 1;
    }
    armUiInputFocus();
    armMouseCapture();

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
            if (command == String("step-forward")) {
                scene_cam_z += 96;
                scene_depth = 1;
                scene_cam_z = clampInt(scene_cam_z, -4096, 4096);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"forward\"}"));
                continue;
            }
            if (command == String("step-back")) {
                scene_cam_z -= 96;
                scene_depth = 0;
                scene_cam_z = clampInt(scene_cam_z, -4096, 4096);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"back\"}"));
                continue;
            }
            if (command == String("step-right")) {
                scene_cam_x += 24;
                scene_lane = clampInt(scene_lane + 24, -320, 320);
                scene_cam_x = clampInt(scene_cam_x, -4096, 4096);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"right\"}"));
                continue;
            }
            if (command == String("step-left")) {
                scene_cam_x -= 24;
                scene_lane = clampInt(scene_lane - 24, -320, 320);
                scene_cam_x = clampInt(scene_cam_x, -4096, 4096);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"left\"}"));
                continue;
            }
            if (command == String("yaw-left")) {
                scene_cam_yaw = wrapDegrees360(scene_cam_yaw - 12);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"yaw-left\"}"));
                continue;
            }
            if (command == String("yaw-right")) {
                scene_cam_yaw = wrapDegrees360(scene_cam_yaw + 12);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"yaw-right\"}"));
                continue;
            }
            if (command == String("pitch-up")) {
                scene_cam_pitch = clampInt(scene_cam_pitch + 4, -89, 89);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"pitch-up\"}"));
                continue;
            }
            if (command == String("pitch-down")) {
                scene_cam_pitch = clampInt(scene_cam_pitch - 4, -89, 89);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"pitch-down\"}"));
                continue;
            }
            if (command == String("pose-reset")) {
                scene_cam_x = 0;
                scene_cam_y = 0;
                scene_cam_z = 0;
                scene_cam_yaw = 0;
                scene_cam_pitch = 0;
                scene_depth = 0;
                scene_lane = 0;
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"pose-reset\"}"));
                continue;
            }
            if (command.indexOf(String("yaw-set ")) == 0) {
                String value = command.substr(8).trim();
                scene_cam_yaw = wrapDegrees360((int)strtol(value.operator char *(), NULL, 10));
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"yaw-set\",\"value\":") + String(scene_cam_yaw) + String("}"));
                continue;
            }
            if (command.indexOf(String("pitch-set ")) == 0) {
                String value = command.substr(10).trim();
                scene_cam_pitch = clampInt((int)strtol(value.operator char *(), NULL, 10), -89, 89);
                sendScenePose();
                pushUiState();
                traceLine(String("{\"event\":\"manual_step\",\"dir\":\"pitch-set\",\"value\":") + String(scene_cam_pitch) + String("}"));
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

    shutdown();
    return 0;
}

}

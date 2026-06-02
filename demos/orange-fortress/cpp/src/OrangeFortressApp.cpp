#include "OrangeFortressApp.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>
#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarasockets/rpc/brpc/BRpcCodec.h>
#include <libelarasockets/rpc/brpc/BRpcRpcCodec.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>
#include "OrangeFortressEpaFrame.h"

namespace elara {
using namespace elara::ui::rpc;
using sockets::rpc::brpc::BRpcReader;
using sockets::rpc::brpc::BRpcRpcCodec;
using sockets::rpc::brpc::BRpcWriter;
using sockets::rpc::brpc::BRPC_ARRAY;
using sockets::rpc::brpc::BRPC_NAMED_BYTE;
using sockets::rpc::brpc::BRPC_NAMED_STRING;

namespace {

static bool path_exists(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static std::string dirname_of(const std::string &path) {
    std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return std::string();
    }
    if (slash == 0) {
        return std::string("/");
    }
    return path.substr(0, slash);
}

static std::string executable_path() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return std::string();
    }
    buf[len] = '\0';
    return std::string(buf);
}

static std::string orange_fortress_root_path() {
    std::string exe = executable_path();
    if (exe.empty()) {
        return std::string();
    }
    return dirname_of(dirname_of(dirname_of(exe)));
}

static std::string orange_fortress_spirv_builder_path() {
    std::string root = orange_fortress_root_path();
    if (root.empty()) {
        return std::string();
    }
    return root + "/shaders/build_spirv_dat.py";
}

static std::string orange_fortress_project_spirv_path() {
    std::string root = orange_fortress_root_path();
    if (root.empty()) {
        return std::string();
    }
    return root + "/shaders/spirv.dat";
}

static bool ensure_directory_exists(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool run_and_wait(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool ensure_project_spirv_dat() {
    std::string spirv_path = orange_fortress_project_spirv_path();
    if (path_exists(spirv_path)) {
        return true;
    }

    std::string builder_path = orange_fortress_spirv_builder_path();
    std::string parent_dir = dirname_of(spirv_path);
    if (spirv_path.empty() || builder_path.empty()) {
        printf("Unable to resolve Orange Fortress SPIR-V paths\n");
        return false;
    }
    if (!path_exists(builder_path)) {
        printf("Missing Orange Fortress SPIR-V builder: %s\n", builder_path.c_str());
        return false;
    }
    if (!ensure_directory_exists(parent_dir)) {
        printf("Unable to create Orange Fortress shader directory: %s\n", parent_dir.c_str());
        return false;
    }

    printf("Building missing Orange Fortress SPIR-V cache: %s\n", spirv_path.c_str());
    char python3_cmd[] = "python3";
    char builder_arg[PATH_MAX];
    char out_arg[PATH_MAX];
    snprintf(builder_arg, sizeof(builder_arg), "%s", builder_path.c_str());
    snprintf(out_arg, sizeof(out_arg), "%s", spirv_path.c_str());
    char *argv[] = { python3_cmd, builder_arg, out_arg, NULL };
    if (!run_and_wait(argv)) {
        printf("Failed to build Orange Fortress SPIR-V cache via %s\n", builder_path.c_str());
        return false;
    }
    if (!path_exists(spirv_path)) {
        printf("SPIR-V builder completed without producing %s\n", spirv_path.c_str());
        return false;
    }
    return true;
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

static int orangeFortressSceneCameraZForDistance(int distance) {
    const int target_z = 2200;
    const int epa_camera_z_bias = 6500;
    return target_z + epa_camera_z_bias - distance;
}

static void appendJsonCommand(String &json, int &emitted, const String &command) {
    if (emitted) {
        json += String(",");
    }
    json += command;
    emitted = 1;
}

static String json_quote_simple(const String &value) {
    String result("\"");
    String value_copy(value);
    const char *raw = value_copy.operator char *();
    if (!raw) {
        result += String("\"");
        return result;
    }
    for (const char *p = raw; *p; ++p) {
        char ch = *p;
        if (ch == '\\' || ch == '"') {
            result += String("\\");
            char tmp[2] = { ch, 0 };
            result += String(tmp);
        } else if (ch == '\n') {
            result += String("\\n");
        } else if (ch == '\r') {
            result += String("\\r");
        } else if (ch == '\t') {
            result += String("\\t");
        } else {
            char tmp[2] = { ch, 0 };
            result += String(tmp);
        }
    }
    result += String("\"");
    return result;
}

static bool elg_read_exact(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool elg_read_frame(int fd, std::vector<char> &frame) {
    unsigned char hdr[4];
    if (!elg_read_exact(fd, (char *)hdr, 4)) return false;
    uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
                 | ((uint32_t)hdr[2] << 8)  | (uint32_t)hdr[3];
    if (len > 4u * 1024u * 1024u) return false;
    frame.resize(len);
    return elg_read_exact(fd, frame.data(), len);
}

static bool elg_write_frame(int fd, const ByteArray &data) {
    uint32_t len = (uint32_t)data.length();
    unsigned char hdr[4] = {
        (unsigned char)(len >> 24), (unsigned char)(len >> 16),
        (unsigned char)(len >> 8),  (unsigned char)len
    };
    if (write(fd, hdr, 4) != 4) return false;
    const char *raw = (const char *)data;
    ssize_t written = write(fd, raw, (size_t)len);
    return written == (ssize_t)len;
}

static void elg_dispatch_frame(OrangeFortressApp *app, int fd, const std::vector<char> &frame) {
    BRpcReader reader(frame.data(), frame.size());
    uint8_t type;
    if (!reader.peekType(type) || type != BRPC_ARRAY) return;
    uint32_t total, count;
    if (!reader.readArrayHeader(total, count)) return;

    String id, method, params_json;
    for (uint32_t i = 0; i < count; i++) {
        if (!reader.peekType(type)) break;
        if (type == BRPC_NAMED_STRING) {
            String name, value;
            if (!reader.readNamedString(name, value)) break;
            if (name == String("id")) id = value;
            else if (name == String("method")) method = value;
            else if (name == String("params")) params_json = value;
        } else if (type == BRPC_NAMED_BYTE) {
            String name; uint8_t bval;
            if (!reader.readNamedByte(name, bval)) break;
        } else {
            reader.skipValue();
        }
    }

    if (!id.length()) return;

    BRpcWriter fields;
    fields.writeNamedString(String("id"), id);
    if (method == String("ext.ping")) {
        fields.writeNamedByte(String("ok"), 1);
        fields.writeNamedString(String("result"), String("\"pong\""));
        BRpcWriter resp;
        resp.writeArray(fields, 3);
        elg_write_frame(fd, resp.bytes());
    } else if (method == String("ext.register")) {
        fields.writeNamedByte(String("ok"), 1);
        fields.writeNamedString(String("result"), String("{\"ok\":true}"));
        BRpcWriter resp;
        resp.writeArray(fields, 3);
        elg_write_frame(fd, resp.bytes());
    } else if (app) {
        String result_json, error_code, error_message;
        if (app->handleExtLogicRequest(method, params_json, result_json, error_code, error_message)) {
            fields.writeNamedByte(String("ok"), 1);
            fields.writeNamedString(String("result"), result_json.length() ? result_json : String("{}"));
            BRpcWriter resp;
            resp.writeArray(fields, 3);
            elg_write_frame(fd, resp.bytes());
        } else {
            fields.writeNamedByte(String("ok"), 0);
            fields.writeNamedString(String("code"), error_code.length() ? error_code : String("not_found"));
            fields.writeNamedString(String("msg"), error_message.length() ? error_message : String("method not implemented on C++ host"));
            BRpcWriter resp;
            resp.writeArray(fields, 4);
            elg_write_frame(fd, resp.bytes());
        }
    } else {
        fields.writeNamedByte(String("ok"), 0);
        fields.writeNamedString(String("code"), String("not_found"));
        fields.writeNamedString(String("msg"), String("method not implemented on C++ host"));
        BRpcWriter resp;
        resp.writeArray(fields, 4);
        elg_write_frame(fd, resp.bytes());
    }
}

class UiEventSinkService : public sockets::rpc::json::JsonRPCService {
public:
    explicit UiEventSinkService(OrangeFortressApp *value_app)
        : sockets::rpc::json::JsonRPCService("ui"),
          app(value_app) {
    }

    void notify(const String& method, const String& params_json) override {
        String result_json;
        String error_code;
        String error_message;
        call(method, params_json, result_json, error_code, error_message);
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
                    printf("ui.event keyDown keyval=%u\n", keyval);
                    fflush(stdout);
                    app->enqueueKeyDown(keyval);
                    app->updateKeyState(keyval, true);
                }
            } else if (app && params.indexOf(String("\"action\":\"keyUp\"")) >= 0) {
                int key_index = params.indexOf(String("\"keyval\":"));
                if (key_index >= 0) {
                    String fragment = params.substr(key_index + 9).trim();
                    unsigned int keyval = (unsigned int)strtoul(fragment.operator char *(), NULL, 10);
                    printf("ui.event keyUp keyval=%u\n", keyval);
                    fflush(stdout);
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
            } else if (app && params.indexOf(String("\"action\":\"mouseScroll\"")) >= 0) {
                double dx = parse_json_number_after(params, String("\"dx\":"), 0.0);
                double dy = parse_json_number_after(params, String("\"dy\":"), 0.0);
                app->handleMouseScroll(dx, dy);
            }

            result_json = "{\"received\":true}";
            return true;
        }

        if (method == String("quit")) {
            if (app) {
                app->ui_quit_requested.store(true);
            }
            result_json = "{\"ok\":true}";
            return true;
        }

        error_code = "method_not_found";
        error_message = "No client-side ui event handler matched the request";
        return false;
    }

private:
    OrangeFortressApp *app;
};

SceneViewState clampSceneViewState(const SceneViewState &input) {
    SceneViewState state = input;
    state.cam_x = clampInt(state.cam_x, -4096, 4096);
    state.cam_y = clampInt(state.cam_y, -1024, 1024);
    state.cam_z = clampInt(state.cam_z, -4096, 4096);
    state.cam_yaw = wrapDegrees360(state.cam_yaw);
    state.cam_pitch = wrapDegrees360(state.cam_pitch);
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

}

OrangeFortressApp::OrangeFortressApp(
    const String &value_host,
    int value_port,
    const OrangeFortressDebugSessionConfig &value_debug_session,
    bool value_prefer_owned_ui_server
)
    : host(value_host),
      port(value_port),
      bundle_path(String("..") + String("/") + String("build") + String("/") + String("epa.bin")),
      bundle_exists(false),
      epa_loaded(false),
      epa_started(false),
      epa_dbg_fd(-1),
      ui_quit_requested(false),
      incremental_ui_supported(true),
      last_section_update_timed_out(false),
      input_lock("orange-fortress-input"),
      render_lock("orange-fortress-render"),
      held_forward(false),
      held_back(false),
      held_left(false),
      held_right(false),
      pending_mouse_dx(0),
      pending_mouse_dy(0),
      pending_mouse_scroll_dy(0),
      mouse_drag_active(false),
      mouse_captured(false),
      mouse_capture_requested(false),
      mouse_uncapture_requested(false),
      scene_cam_x(0),
      scene_cam_y(0),
      scene_cam_z(0),
      scene_cam_yaw(0),
      scene_cam_pitch(0),
      scene_orbit_distance(8700),
      scene_depth(0),
      scene_lane(0),
      cached_scene_angle(0),
      latest_surface_valid(false),
      scene_received(false),
      surface_revision(0),
      pushed_surface_revision(0),
      trace_path(String("..") + String("/") + String("artifacts") + String("/") + String("live-epa-trace.jsonl")),
      trace_file(NULL),
      trace_sequence(0),
      peer(new ElaraUiRpcPeer()),
      debug_session(value_debug_session),
      host_debug_fd(-1),
      ext_logic_server_fd(-1),
      owned_ui_server_pid(-1),
      prefer_owned_ui_server(value_prefer_owned_ui_server) {
    if (debug_session.enabled && debug_session.bundle_path.length()) {
        bundle_path = debug_session.bundle_path;
    }
}

OrangeFortressApp::~OrangeFortressApp() {
    shutdown();
}

void OrangeFortressApp::openTraceArtifact() {
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

void OrangeFortressApp::closeTraceArtifact() {
    if (!trace_file) {
        return;
    }
    traceLine(String("{\"event\":\"trace_close\"}"));
    fclose(trace_file);
    trace_file = NULL;
}

bool OrangeFortressApp::failIfUiDisconnected(const char *context) {
    if (peer && peer->isConnected()) {
        return false;
    }
    const char *where = context ? context : "unknown";
    printf("UI connection lost during %s. C++ host exiting.\n", where);
    traceLine(String("{\"event\":\"ui_connection_lost\",\"context\":")
        + json_quote_simple(String(where))
        + String(",\"action\":\"host_exit\"}"));
    sendHostDebugLog(String("UI connection lost during ") + String(where) + String("; host exiting"));
    return true;
}

bool OrangeFortressApp::connectHostDebugBridge() {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    char port_text[32];

    if (!debug_session.enabled || !debug_session.host_debug_host.length() || debug_session.host_debug_port <= 0) {
        return false;
    }
    if (host_debug_fd >= 0) {
        return true;
    }

    printf("[C++ Host] connecting to IDE bridge %s:%d\n",
           debug_session.host_debug_host.operator char *(),
           debug_session.host_debug_port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", debug_session.host_debug_port);
    if (getaddrinfo(debug_session.host_debug_host.operator char *(), port_text, &hints, &result) != 0) {
        printf("[C++ Host] IDE bridge connect failed: getaddrinfo error\n");
        return false;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            host_debug_fd = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(result);
    if (host_debug_fd >= 0) {
        printf("[C++ Host] IDE bridge connected\n");
        startHostDebugReader();
    } else {
        printf("[C++ Host] IDE bridge connect failed\n");
    }
    return host_debug_fd >= 0;
}

void OrangeFortressApp::closeHostDebugBridge() {
    if (host_debug_fd >= 0) {
        close(host_debug_fd);
        host_debug_fd = -1;
    }
}

void OrangeFortressApp::sendHostDebugEvent(const String &kind, const String &payload_json) {
    String message;
    if (!connectHostDebugBridge()) {
        return;
    }
    message = String("{\"kind\":") + json_quote_simple(kind)
        + String(",\"session_id\":") + json_quote_simple(debug_session.session_id)
        + String(",\"project\":") + json_quote_simple(String("orange-fortress"));
    if (payload_json.length()) {
        message += String(",") + payload_json;
    }
    message += String("}\n");
    std::lock_guard<std::mutex> lock(host_debug_io_mutex);
    if (host_debug_fd < 0) {
        return;
    }
    if (write(host_debug_fd, message.operator char *(), (size_t)message.length()) < 0) {
        closeHostDebugBridge();
    }
}

void OrangeFortressApp::sendHostDebugLog(const String &message) {
    sendHostDebugEvent(String("log"), String("\"message\":") + json_quote_simple(message));
}

void OrangeFortressApp::sendHostDebugState(const String &status) {
    sendHostDebugEvent(String("state"), String("\"status\":") + json_quote_simple(status));
}

void OrangeFortressApp::startHostDebugReader() {
    std::thread(&OrangeFortressApp::hostDebugReadLoop, this).detach();
}

void OrangeFortressApp::hostDebugReadLoop() {
    std::string pending;
    char buffer[512];

    while (true) {
        int fd_snapshot;
        {
            std::lock_guard<std::mutex> lock(host_debug_io_mutex);
            fd_snapshot = host_debug_fd;
        }
        if (fd_snapshot < 0) {
            break;
        }

        ssize_t n = read(fd_snapshot, buffer, sizeof(buffer));
        if (n <= 0) {
            closeHostDebugBridge();
            break;
        }
        pending.append(buffer, (size_t)n);

        while (true) {
            std::string::size_type nl = pending.find('\n');
            if (nl == std::string::npos) {
                break;
            }

            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1u);
            if (line.empty()) {
                continue;
            }

            try {
                Json payload(String(line.c_str()));
                String kind = payload.getStringValue("kind");
                if (kind == String("ping")) {
                    String req_id = payload.getStringValue("id");
                    sendHostDebugEvent(String("pong"), String("\"id\":") + json_quote_simple(req_id));
                } else if (kind == String("quit")) {
                    printf("[C++ Host] IDE sent quit — shutting down cleanly.\n");
                    fflush(stdout);
                    ui_quit_requested.store(true);
                }
            } catch (...) {
            }
        }
    }
}

int OrangeFortressApp::chooseUiFallbackPort() const {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) != 0) {
        close(fd);
        return 0;
    }
    int chosen_port = (int)ntohs(addr.sin_port);
    close(fd);
    return chosen_port;
}

void OrangeFortressApp::recordLaunchedPid(const char *label, pid_t pid) const {
    const char *pid_file = getenv("ELARA_PID_FILE");
    if (!pid_file || !pid_file[0] || pid <= 0) {
        return;
    }
    FILE *out = fopen(pid_file, "a");
    if (!out) {
        return;
    }
    fprintf(out, "%d\t%s\n", (int)pid, label ? label : "orange-fortress-ui-head");
    fclose(out);
}

void OrangeFortressApp::stopOwnedUiServer() {
    if (owned_ui_server_pid <= 0) {
        return;
    }
    pid_t pid = owned_ui_server_pid;
    owned_ui_server_pid = -1;
    kill(pid, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            return;
        }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

bool OrangeFortressApp::launchUiServerFallback() {
    if (owned_ui_server_pid > 0) {
        return true;
    }
    int fallback_port = chooseUiFallbackPort();
    if (fallback_port <= 0) {
        printf("Failed to choose a fallback UI port\n");
        return false;
    }

    char port_text[32];
    snprintf(port_text, sizeof(port_text), "%d", fallback_port);
    std::string spirv_path = orange_fortress_project_spirv_path();
    if (!ensure_project_spirv_dat()) {
        printf("Proceeding with embedded Vulkan shader fallback\n");
    } else if (!spirv_path.empty()) {
        printf("Using Orange Fortress SPIR-V cache: %s\n", spirv_path.c_str());
    }
    const char *display_env = getenv("DISPLAY");
    const char *wayland_env = getenv("WAYLAND_DISPLAY");
    const char *xauth_env = getenv("XAUTHORITY");
    const char *dbus_env = getenv("DBUS_SESSION_BUS_ADDRESS");
    printf("Fallback UI env: DISPLAY=%s WAYLAND_DISPLAY=%s XAUTHORITY=%s DBUS_SESSION_BUS_ADDRESS=%s\n",
           display_env && display_env[0] ? display_env : "(unset)",
           wayland_env && wayland_env[0] ? wayland_env : "(unset)",
           xauth_env && xauth_env[0] ? xauth_env : "(unset)",
           dbus_env && dbus_env[0] ? dbus_env : "(unset)");

    pid_t pid = fork();
    if (pid < 0) {
        printf("Failed to fork elaraui-server launcher: %s\n", strerror(errno));
        return false;
    }
    if (pid == 0) {
        if (!spirv_path.empty()) {
            setenv("ELARA_VULKAN_SURFACE_SPIRV_PATH", spirv_path.c_str(), 1);
        }
        String backend_id = String("org.elara.ui.orange-fortress.p") + String(fallback_port);
        execlp("elaraui-server", "elaraui-server",
               "--port", port_text,
               "--backend-id", backend_id.operator char *(),
               "--persistent",
               (char *)NULL);
        fprintf(stderr, "Failed to exec elaraui-server: %s\n", strerror(errno));
        _exit(127);
    }

    owned_ui_server_pid = pid;
    recordLaunchedPid("orange-fortress-ui-head", pid);
    host = String("127.0.0.1");
    port = fallback_port;
    printf("Spawned elaraui-server pid=%d on fallback port %d backend_id=%s\n",
           (int)pid,
           port,
           (String("org.elara.ui.orange-fortress.p") + String(fallback_port)).operator char *());

    for (int attempt = 0; attempt < 40; ++attempt) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            owned_ui_server_pid = -1;
            if (WIFEXITED(status)) {
                printf("elaraui-server exited before accepting connections with code %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("elaraui-server exited before accepting connections from signal %d\n", WTERMSIG(status));
            } else {
                printf("elaraui-server exited before accepting connections with status 0x%x\n", status);
            }
            return false;
        }
        usleep(100000);
        if (peer->connect(host, (unsigned short)port)) {
            return true;
        }
    }

    printf("Timed out waiting for fallback elaraui-server on %s:%d\n", host.operator char *(), port);
    stopOwnedUiServer();
    return false;
}

bool OrangeFortressApp::connectUiPeer() {
    if (prefer_owned_ui_server) {
        printf("Launching dedicated elaraui-server for Orange Fortress\n");
        return launchUiServerFallback();
    }
    if (peer->connect(host, (unsigned short)port)) {
        return true;
    }
    printf("Failed to connect to %s:%d\n", host.operator char *(), port);
    return launchUiServerFallback();
}

void OrangeFortressApp::startExtLogicServer() {
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return;
    }
    socklen_t addrlen = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &addrlen);
    int port_num = (int)ntohs(addr.sin_port);
    if (::listen(fd, 1) != 0) {
        close(fd);
        return;
    }
    ext_logic_server_fd = fd;
    sendHostDebugEvent(String("ext_logic_listen"), String("\"port\":") + String(port_num));
    ext_logic_thread = std::thread(&OrangeFortressApp::extLogicServe, this);
    ext_logic_thread.detach();
}

void OrangeFortressApp::extLogicServe() {
    while (ext_logic_server_fd >= 0) {
        int client = accept(ext_logic_server_fd, NULL, NULL);
        if (client < 0) break;
        std::vector<char> frame;
        while (elg_read_frame(client, frame)) {
            elg_dispatch_frame(this, client, frame);
        }
        close(client);
    }
}

void OrangeFortressApp::armUiInputFocus() {
    String result_json;
    String error_code;
    String error_message;

    peer->call(String("ui.enableEvent"), String("{\"action\":\"keyDown\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"keyUp\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseMove\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseDown\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseUp\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.enableEvent"), String("{\"action\":\"mouseScroll\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.setFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);
    peer->call(String("ui.lockFocus"), String("{\"target\":\"app.surface\"}"), result_json, error_code, error_message, 5000);

    traceLine(String("{\"event\":\"ui_input_focus_armed\",\"target\":\"app.surface\"}"));
}

void OrangeFortressApp::armMouseCapture() {
    setMouseCaptured(false);
    traceLine(String("{\"event\":\"mouse_capture_armed_click_surface\"}"));
}

void OrangeFortressApp::setMouseCaptured(bool captured) {
    String result_json;
    String error_code;
    String error_message;
    mouse_captured = captured;
    mouse_capture_requested = false;
    mouse_uncapture_requested = false;
    {
        Mutex::Lock lock(input_lock);
        if (!captured) {
            mouse_drag_active = false;
        }
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
        pending_mouse_scroll_dy = 0;
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

void OrangeFortressApp::traceLine(const String &json_line) {
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

void OrangeFortressApp::traceKernelStateSnapshot(const char *phase) {
    if (!epa_loaded) {
        traceLine(String("{\"event\":\"kernel_snapshot\",\"phase\":")
            + JsonString(String(phase ? phase : "unknown"), true).toString()
            + String(",\"ready\":false}"));
    }
}

void OrangeFortressApp::refreshProjectState() {
    bundle_exists = access(bundle_path.operator char *(), F_OK) == 0;
}

bool OrangeFortressApp::connectEpaDbg() {
    struct addrinfo hints, *result = NULL, *rp = NULL;
    char port_text[32];
    if (epa_dbg_fd >= 0) return true;
    if (!debug_session.enabled || !debug_session.epa_dbg_host.length() || debug_session.epa_dbg_port <= 0)
        return false;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", debug_session.epa_dbg_port);
    if (getaddrinfo(debug_session.epa_dbg_host.operator char *(), port_text, &hints, &result) != 0)
        return false;
    for (rp = result; rp; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) { epa_dbg_fd = fd; break; }
        close(fd);
    }
    freeaddrinfo(result);
    if (epa_dbg_fd >= 0)
        printf("[C++ Host] connected to EPA debug VM at %s:%d\n",
               debug_session.epa_dbg_host.operator char *(), debug_session.epa_dbg_port);
    else
        printf("[C++ Host] failed to connect to EPA debug VM at %s:%d\n",
               debug_session.epa_dbg_host.operator char *(), debug_session.epa_dbg_port);
    return epa_dbg_fd >= 0;
}

void OrangeFortressApp::closeEpaDbg() {
    if (epa_dbg_fd >= 0) { close(epa_dbg_fd); epa_dbg_fd = -1; }
}

bool OrangeFortressApp::epaDbgCall(const String &method, const String &params_json, String &result_json) {
    std::lock_guard<std::mutex> lock(epa_dbg_mutex);
    if (epa_dbg_fd < 0) return false;

    ByteArray request = BRpcRpcCodec::buildRequest(
        String("orange-fortress"),
        method,
        params_json.length() ? params_json : String("{}")
    );
    ByteArray frame = BRpcRpcCodec::framePayload(request);
    if (write(epa_dbg_fd, frame.operator const char *(), frame.length()) != (ssize_t)frame.length()) {
        closeEpaDbg();
        return false;
    }

    // Read length-prefixed response.
    unsigned char rhdr[4];
    if (!elg_read_exact(epa_dbg_fd, (char *)rhdr, 4)) { closeEpaDbg(); return false; }
    uint32_t rlen = ((uint32_t)rhdr[0] << 24) | ((uint32_t)rhdr[1] << 16)
                  | ((uint32_t)rhdr[2] << 8)  |  (uint32_t)rhdr[3];
    if (rlen > 4u * 1024u * 1024u) { closeEpaDbg(); return false; }
    std::vector<char> rbuf(rlen + 1, 0);
    if (!elg_read_exact(epa_dbg_fd, rbuf.data(), rlen)) { closeEpaDbg(); return false; }

    String response_id;
    bool ok = false;
    String response_result_json;
    String response_error_code;
    String response_error_message;
    String parse_error_message;
    if (!BRpcRpcCodec::parseResponse(
            rbuf.data(),
            (size_t)rlen,
            response_id,
            ok,
            response_result_json,
            response_error_code,
            response_error_message,
            parse_error_message)) {
        printf("[C++ Host] EPA debug VM parseResponse failed: %s\n",
               parse_error_message.operator char *());
        closeEpaDbg();
        return false;
    }
    if (!ok) {
        String method_copy(method);
        printf("[C++ Host] EPA debug VM RPC failed method=%s code=%s msg=%s\n",
               method_copy.operator char *(),
               response_error_code.operator char *(),
               response_error_message.operator char *());
        return false;
    }
    result_json = response_result_json;
    return true;
}

void OrangeFortressApp::drainEpaDebugEvents() {
    String events_json;
    if (!epaDbgCall(String("epa.debug.events"), String("{\"clear\":true}"), events_json)) {
        return;
    }
    try {
        Json json(events_json);
        Array< Ref<JsonValue> > events = json.getArray(String("events"));
        for (unsigned int i = 0; i < events.length(); i++) {
            Json event(events[i]->toString());
            String kind = event.getStringValue(String("kind"));
            String message = event.getStringValue(String("message"));
            if (kind == String("log") && message.length()) {
                sendHostDebugLog(message);
            }
        }
    } catch (...) {
        if (events_json.length()) {
            sendHostDebugLog(String("[epa vm] failed to parse debug events: ") + events_json);
        }
    }
}

bool OrangeFortressApp::epaDbgLoadBundle() {
    refreshProjectState();
    epa_loaded = false;
    epa_started = false;
    {
        Mutex::Lock lock(render_lock);
        latest_surface_valid = false;
        latest_surface_commands = String();
        scene_received = false;
        surface_revision = 0;
        pushed_surface_revision = 0;
    }
    traceLine(String("{\"event\":\"epa_dbg_load_bundle\",\"bundle_exists\":") + String(bundle_exists ? "true" : "false") + String("}"));
    if (!bundle_exists) {
        sendHostDebugState(String("Status: bundle missing"));
        return false;
    }
    if (!connectEpaDbg()) {
        sendHostDebugState(String("Status: EPA debug VM not reachable"));
        return false;
    }
    String result_json;
    String params = String("{\"bundle_path\":") + json_quote_simple(bundle_path) + String("}");
    if (!epaDbgCall(String("epa.debug.loadBundle"), params, result_json)) {
        printf("[C++ Host] EPA debug VM: loadBundle failed for %s\n", bundle_path.operator char *());
        sendHostDebugState(String("Status: bundle load failed in EPA VM"));
        return false;
    }
    epa_loaded = true;
    epa_started = true;
    printf("[C++ Host] EPA debug VM: bundle loaded %s\n", bundle_path.operator char *());
    sendHostDebugState(String("Status: bundle loaded in EPA VM"));
    traceLine(String("{\"event\":\"epa_dbg_bundle_loaded\"}"));
    return true;
}

bool OrangeFortressApp::sendScenePose() {
    struct ScenePoseInputPayload {
        int32_t cam_x, cam_z, depth, lane, yaw, pitch;
        int32_t end_wall_x, end_wall_y, end_wall_w, end_wall_h, end_wall_visible;
        int32_t side_wall_x, side_wall_y, side_wall_w, side_wall_h, side_wall_visible;
        int32_t marker0_x, marker0_y, marker0_visible;
        int32_t marker1_x, marker1_y, marker1_visible;
        int32_t marker2_x, marker2_y, marker2_visible;
    };

    if (!epa_started) return false;

    CalibrationProjectionState projection = projectCalibrationScene(
        SceneViewState{ scene_cam_x, scene_cam_y, scene_cam_z, scene_cam_yaw, scene_cam_pitch, scene_depth, scene_lane }
    );
    ScenePoseInputPayload pose = {
        scene_cam_x, scene_cam_z, scene_depth, scene_lane,
        wrapDegrees360(scene_cam_yaw), wrapDegrees360(scene_cam_pitch),
        projection.end_wall.x, projection.end_wall.y, projection.end_wall.w, projection.end_wall.h, projection.end_wall.visible ? 1 : 0,
        projection.side_wall.x, projection.side_wall.y, projection.side_wall.w, projection.side_wall.h, projection.side_wall.visible ? 1 : 0,
        projection.marker0.x, projection.marker0.y, projection.marker0.visible ? 1 : 0,
        projection.marker1.x, projection.marker1.y, projection.marker1.visible ? 1 : 0,
        projection.marker2.x, projection.marker2.y, projection.marker2.visible ? 1 : 0
    };
    printf("sendScenePose: cam_x=%d cam_z=%d yaw=%d pitch=%d\n",
           pose.cam_x, pose.cam_z, pose.yaw, pose.pitch);
    fflush(stdout);
    traceLine(String("{\"event\":\"send_scene_pose\",\"cam_x\":") + String(pose.cam_x)
        + String(",\"cam_z\":") + String(pose.cam_z) + String("}"));

    // Encode payload bytes as hex and push via EPA debug VM.
    const unsigned char *raw = (const unsigned char *)&pose;
    String hex;
    char tmp[3];
    for (size_t i = 0; i < sizeof(pose); i++) {
        snprintf(tmp, sizeof(tmp), "%02x", (unsigned)raw[i]);
        hex += String(tmp);
    }
    String epa_scene_path_id("scene");
    String ingress_params = String("{\"path_id\":") + json_quote_simple(epa_scene_path_id)
                          + String(",\"wid\":1,\"payload_hex\":\"")
                          + hex + String("\"}");
    String result_json;
    String clear_mailbox_result;
    String scene_kernel_params = String("{\"path_id\":") + json_quote_simple(epa_scene_path_id) + String("}");
    if (!epaDbgCall(String("epa.debug.clearMailbox"), scene_kernel_params, clear_mailbox_result)) {
        printf("sendScenePose: clearMailbox failed\n");
        return false;
    }
    printf("sendScenePose: mailbox cleared\n");
    if (!epaDbgCall(String("epa.debug.ingressPushHex"), ingress_params, result_json)) {
        printf("sendScenePose: ingressPushHex failed\n");
        return false;
    }
    printf("sendScenePose: ingressPushHex queued=%s\n", result_json.operator char *());
    String ingress_frame_json = orangeFortressIngressFrameHeaderJson(
        String("orange-fortress.epa.frame.v1"),
        String("ScenePoseInputPayload"),
        (uint32_t)pose.depth,
        (uint32_t)sizeof(pose)
    );
    sendHostDebugEvent(
        String("ingress_frame"),
        String("\"kernel\":\"orange.fortress.scene\",")
            + String("\"worker\":\"wid=1\",")
            + String("\"frame\":") + ingress_frame_json
    );
    sendHostDebugEvent(
        String("ingress"),
        String("\"kernel\":\"orange.fortress.scene\",")
            + String("\"worker\":\"wid=1\",")
            + String("\"type\":\"ScenePoseInputPayload\",")
            + String("\"frame\":") + ingress_frame_json + String(",")
            + String("\"details\":")
            + json_quote_simple(
                String("cam_x=") + String(pose.cam_x)
                + String(" cam_z=") + String(pose.cam_z)
                + String(" yaw=") + String(pose.yaw)
                + String(" pitch=") + String(pose.pitch)
            )
    );

    // Run the scene kernel until signal (E3SB frame commit) or max ticks.
    String run_result;
    if (!epaDbgCall(String("epa.debug.run"),
                    String("{\"path_id\":") + json_quote_simple(epa_scene_path_id) + String(",\"max_ticks\":200000}"),
                    run_result)) {
        drainEpaDebugEvents();
        printf("sendScenePose: run failed\n");
        return false;
    }
    drainEpaDebugEvents();
    printf("sendScenePose: run result=%s\n", run_result.operator char *());

    // Retrieve the mailbox bytes produced by frame_commit.
    String mailbox_result;
    if (!epaDbgCall(String("epa.debug.getMailbox"), scene_kernel_params, mailbox_result)) {
        printf("sendScenePose: getMailbox failed\n");
        return false;
    }
    printf("sendScenePose: mailbox result=%s\n", mailbox_result.operator char *());
    try {
        Json mb(mailbox_result);
        String hex_str = mb.getStringValue("hex");
        int wid_val = (int)mb.getIntValue("wid");
        int len_val = (int)mb.getIntValue("len");
        if (len_val <= 0 || !hex_str.length()) {
            printf("sendScenePose: mailbox empty after run wid=%d len=%d\n", wid_val, len_val);
            sendHostDebugLog(String("mailbox failure: empty mailbox after scene run wid=")
                + String(wid_val) + String(" len=") + String(len_val));
            sendHostDebugState(String("Status: mailbox empty after scene run"));
            return false;
        }
        std::vector<char> bytes(len_val);
        String hex_copy(hex_str);
        const char *p = hex_copy.operator char *();
        for (int i = 0; i < len_val && p && p[0] && p[1]; i++, p += 2) {
            char chunk[3] = { p[0], p[1], 0 };
            bytes[i] = (char)strtoul(chunk, NULL, 16);
        }
        sendHostDebugEvent(
            String("egress"),
            String("\"kernel\":\"orange.fortress.scene\",")
                + String("\"worker\":\"wid=") + String(wid_val) + String("\",")
                + String("\"signal\":\"SceneFrameMailbox\",")
                + String("\"frame\":") + orangeFortressFrameHeaderJson(
                    orangeFortressParseEgressFrameHeader((const unsigned char *)bytes.data(), (size_t)len_val),
                    String("egress"),
                    String("orange-fortress.epa.frame.v1")
                ) + String(",")
                + String("\"details\":")
                + json_quote_simple(String("len=") + String(len_val))
        );
        if (len_val >= 4) {
            printf("sendScenePose: mailbox wid=%d len=%d magic=0x%08x\n",
                   wid_val, len_val, (unsigned)orangeFortressReadLeU32((const unsigned char *)bytes.data()));
        } else {
            printf("sendScenePose: mailbox wid=%d len=%d\n", wid_val, len_val);
        }
        if (!updateSurfaceCommandsFromMailbox((unsigned int)wid_val, bytes.data(), len_val)) {
            return false;
        }
    } catch (...) {
        printf("sendScenePose: mailbox parse failed\n");
        sendHostDebugLog(String("mailbox parse failed after scene ingress"));
        sendHostDebugState(String("Status: mailbox parse failure"));
        return false;
    }
    return true;
}

void OrangeFortressApp::enqueueKeyDown(unsigned int keyval) {
    Mutex::Lock lock(input_lock);
    pending_keydowns.push(keyval);
}

void OrangeFortressApp::updateKeyState(unsigned int keyval, bool pressed) {
    Mutex::Lock lock(input_lock);
    printf("updateKeyState: keyval=%u pressed=%d held_f=%d held_b=%d held_l=%d held_r=%d\n",
           keyval, (int)pressed, (int)held_forward, (int)held_back, (int)held_left, (int)held_right);
    fflush(stdout);
    // Arrow keys and WASD rotate the camera relatively around the scene.
    if (keyval == 65362u || keyval == 119u) { held_forward = pressed; }  // Up / W
    if (keyval == 65364u || keyval == 115u) { held_back    = pressed; }  // Down / S
    if (keyval == 65361u || keyval == 97u)  { held_left    = pressed; }  // Left / A
    if (keyval == 65363u || keyval == 100u) { held_right   = pressed; }  // Right / D
    if (keyval == 65307u && pressed) {
        mouse_uncapture_requested = true;
    }
}

void OrangeFortressApp::accumulateMouseDelta(int dx, int dy) {
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

void OrangeFortressApp::handleMouseScroll(double dx, double dy) {
    (void)dx;
    Mutex::Lock lock(input_lock);
    int steps = (int)lrint(dy);
    if (steps == 0 && dy != 0.0) {
        steps = dy > 0.0 ? 1 : -1;
    }
    pending_mouse_scroll_dy += steps;
    if (pending_mouse_scroll_dy > 12) { pending_mouse_scroll_dy = 12; }
    if (pending_mouse_scroll_dy < -12) { pending_mouse_scroll_dy = -12; }
}

void OrangeFortressApp::handleMouseDown(int button, double x, double y) {
    (void)x;
    (void)y;
    printf("handleMouseDown: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1) {
        Mutex::Lock lock(input_lock);
        mouse_drag_active = true;
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
        mouse_capture_requested = true;
        mouse_uncapture_requested = false;
    }
}

void OrangeFortressApp::handleMouseUp(int button, double x, double y) {
    printf("handleMouseUp: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1) {
        Mutex::Lock lock(input_lock);
        mouse_drag_active = false;
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
        mouse_capture_requested = false;
        mouse_uncapture_requested = true;
    }
}

void OrangeFortressApp::handleMouseMove(double x, double y) {
    {
        Mutex::Lock lock(input_lock);
        if (!mouse_drag_active) {
            return;
        }
    }
    accumulateMouseDelta((int)x, (int)y);
}

void OrangeFortressApp::updateSceneCameraFromOrbit() {
    scene_orbit_distance = clampInt(scene_orbit_distance, 1800, 16000);
    scene_cam_z = orangeFortressSceneCameraZForDistance(scene_orbit_distance);
    scene_cam_z = clampInt(scene_cam_z, -4096, 4096);
    scene_cam_yaw = wrapDegrees360(scene_cam_yaw);
    scene_cam_pitch = wrapDegrees360(scene_cam_pitch);
}

void OrangeFortressApp::publishCachedCubeScene(int angle) {
    if (angle < 0) {
        angle = 0;
    }
    if (angle > 2) {
        angle = 2;
    }
    {
        Mutex::Lock lock(render_lock);
        cached_scene_angle = angle;
        latest_surface_commands = buildCachedCubeSceneJson(angle);
        latest_surface_valid = true;
        surface_revision++;
    }
    printf("Cached cube scene angle=%d queued for replay\n", angle + 1);
    traceLine(String("{\"event\":\"cached_cube_scene\",\"angle\":") + String(angle + 1) + String("}"));
}

String OrangeFortressApp::buildHostDebugSurfaceTestJson() const {
    return String("[")
        + String("{\"op\":\"clear\",\"r\":0.02,\"g\":0.025,\"b\":0.035},")
        + String("{\"op\":\"rect\",\"x\":120,\"y\":110,\"w\":360,\"h\":220,\"r\":1.0,\"g\":0.05,\"b\":0.05},")
        + String("{\"op\":\"line\",\"x0\":60,\"y0\":60,\"x1\":1220,\"y1\":660,\"r\":0.05,\"g\":1.0,\"b\":0.05},")
        + String("{\"op\":\"line\",\"x0\":1220,\"y0\":60,\"x1\":60,\"y1\":660,\"r\":0.05,\"g\":0.45,\"b\":1.0},")
        + String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Host debug surface test\",\"size\":30,\"r\":1.0,\"g\":0.95,\"b\":0.85},")
        + String("{\"op\":\"text\",\"x\":56,\"y\":88,\"text\":\"clear + rect + two lines from C++ host\",\"size\":17,\"r\":0.82,\"g\":0.90,\"b\":0.78}")
        + String("]");
}

bool OrangeFortressApp::handleExtLogicRequest(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("ext.debug.status")) {
        bool drag_active = false;
        int pending_dx = 0;
        int pending_dy = 0;
        int pending_scroll = 0;
        bool captured = false;
        {
            Mutex::Lock input(input_lock);
            drag_active = mouse_drag_active;
            pending_dx = pending_mouse_dx;
            pending_dy = pending_mouse_dy;
            pending_scroll = pending_mouse_scroll_dy;
            captured = mouse_captured;
        }
        Mutex::Lock lock(render_lock);
        result_json = String("{\"epa_started\":") + String(epa_started ? "true" : "false")
            + String(",\"latest_surface_valid\":") + String(latest_surface_valid ? "true" : "false")
            + String(",\"surface_revision\":") + String((int)surface_revision)
            + String(",\"pushed_surface_revision\":") + String((int)pushed_surface_revision)
            + String(",\"mouse_captured\":") + String(captured ? "true" : "false")
            + String(",\"mouse_drag_active\":") + String(drag_active ? "true" : "false")
            + String(",\"pending_mouse_dx\":") + String(pending_dx)
            + String(",\"pending_mouse_dy\":") + String(pending_dy)
            + String(",\"pending_mouse_scroll_dy\":") + String(pending_scroll)
            + String(",\"scene_cam_x\":") + String(scene_cam_x)
            + String(",\"scene_cam_z\":") + String(scene_cam_z)
            + String(",\"scene_cam_yaw\":") + String(scene_cam_yaw)
            + String(",\"scene_cam_pitch\":") + String(scene_cam_pitch)
            + String(",\"scene_orbit_distance\":") + String(scene_orbit_distance)
            + String(",\"cached_scene_angle\":") + String(cached_scene_angle + 1)
            + String("}");
        return true;
    }
    if (method == String("ext.debug.surfaceTest")) {
        {
            Mutex::Lock lock(render_lock);
            latest_surface_commands = buildHostDebugSurfaceTestJson();
            latest_surface_valid = true;
            surface_revision++;
        }
        sendHostDebugLog(String("host debug surface test queued"));
        result_json = String("{\"queued\":true,\"mode\":\"surfaceTest\"}");
        return true;
    }
    if (method == String("ext.debug.cachedCube")) {
        int angle = 0;
        try {
            Json params(params_json.length() ? params_json : String("{}"));
            angle = (int)params.getIntValue(String("angle")) - 1;
        } catch (...) {
            angle = 0;
        }
        publishCachedCubeScene(angle);
        result_json = String("{\"queued\":true,\"mode\":\"cachedCube\",\"angle\":") + String(angle + 1) + String("}");
        return true;
    }
    if (method == String("ext.debug.epaScene")) {
        bool ok = sendScenePose();
        result_json = String("{\"queued\":") + String(ok ? "true" : "false") + String(",\"mode\":\"epaScene\"}");
        return ok;
    }
    error_code = String("not_found");
    error_message = String("method not implemented on C++ host");
    return false;
}

void OrangeFortressApp::drainKeyEvents() {
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
    int scroll_dy = 0;
    {
        Mutex::Lock lock(input_lock);
        move_x = (held_right ? 1 : 0) - (held_left ? 1 : 0);
        move_z = (held_forward ? 1 : 0) - (held_back ? 1 : 0);
        look_dx = pending_mouse_dx;
        look_dy = pending_mouse_dy;
        scroll_dy = pending_mouse_scroll_dy;
        pending_mouse_dx = 0;
        pending_mouse_dy = 0;
        pending_mouse_scroll_dy = 0;
        pending_keydowns.clear();
    }

    if (move_x == 0 && move_z == 0 && look_dx == 0 && look_dy == 0 && scroll_dy == 0) {
        return;
    }

    scene_cam_yaw = wrapDegrees360(scene_cam_yaw + (move_x * 3) + (look_dx / 4));
    scene_cam_pitch = wrapDegrees360(scene_cam_pitch + (move_z * 3) - (look_dy / 5));
    scene_orbit_distance += (scroll_dy * 450);
    updateSceneCameraFromOrbit();
    scene_depth = clampInt((scene_orbit_distance - 1800) / 1800, 0, 6);

    printf("drainCameraInput: distance=%d cam_x=%d cam_z=%d depth=%d yaw=%d pitch=%d\n",
           scene_orbit_distance, scene_cam_x, scene_cam_z, scene_depth, scene_cam_yaw, scene_cam_pitch);
    sendScenePose();
}

void OrangeFortressApp::shutdown() {
    closeEpaDbg();

    if (peer) {
        peer->close();
        peer = Ref<ui::rpc::ElaraUiRpcPeer>(0);
    }

    stopOwnedUiServer();

    if (ext_logic_server_fd >= 0) {
        close(ext_logic_server_fd);
        ext_logic_server_fd = -1;
    }
    closeHostDebugBridge();
    closeTraceArtifact();
}

bool OrangeFortressApp::updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len) {
    const unsigned char *bytes = (const unsigned char *)msg;
    size_t offset = 0;
    String json("[");
    String rect_summary("");
    int emitted = 0;
    int rect_count = 0;
    printf("surface mailbox callback wid=%u len=%d\n", wid, msg_len);
    traceLine(String("{\"event\":\"mailbox_callback\",\"wid\":") + String((int)wid) + String(",\"len\":") + String(msg_len) + String("}"));
    if ((wid != 1u && wid != 2u) || !msg) {
        printf("surface mailbox ignored: invalid wid/msg/len\n");
        traceLine(String("{\"event\":\"mailbox_ignored\",\"reason\":\"invalid_wid_or_len\"}"));
        sendHostDebugLog(String("mailbox failure: invalid worker or payload length len=") + String(msg_len));
        sendHostDebugState(String("Status: mailbox size failure"));
        return false;
    }
    OrangeFortressEpaFrameHeader frame_header = orangeFortressParseEgressFrameHeader(bytes, (size_t)msg_len);
    sendHostDebugEvent(
        String("egress_frame"),
        String("\"kernel\":\"orange.fortress.scene\",")
            + String("\"worker\":\"wid=") + String((int)wid) + String("\",")
            + String("\"frame\":") + orangeFortressFrameHeaderJson(frame_header, String("egress"), String("orange-fortress.epa.frame.v1"))
    );
    if (!frame_header.valid) {
        printf("surface mailbox ignored: frame header error=%s magic=0x%08x len=%d\n",
               frame_header.error.operator char *(), (unsigned)frame_header.magic, msg_len);
        traceLine(String("{\"event\":\"mailbox_ignored\",\"reason\":")
            + JsonString(frame_header.error, true).toString()
            + String(",\"magic\":") + String((int)frame_header.magic) + String("}"));
        sendHostDebugLog(String("mailbox failure: ") + frame_header.error + String(" len=") + String(msg_len));
        sendHostDebugState(String("Status: mailbox frame failure"));
        return false;
    }
    offset = frame_header.header_bytes;
    uint32_t width = frame_header.width;
    uint32_t height = frame_header.height;
    uint32_t frame_type = frame_header.frame_type;
    uint32_t frame_id = frame_header.frame_id;
    uint32_t record_count = frame_header.record_count;

    if (frame_type == 3u) {
        while (offset + 4u <= (size_t)msg_len) {
            uint32_t record_opcode = orangeFortressReadLeU32(bytes + offset);
            offset += 4;
            if (record_opcode == 255u) {
                break;
            }
            if (record_opcode != 2u || offset + 32u > (size_t)msg_len) {
                break;
            }
            int32_t op = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a0 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a1 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a2 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a3 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a4 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a5 = orangeFortressReadLeI32(bytes + offset); offset += 4;
            int32_t a6 = orangeFortressReadLeI32(bytes + offset); offset += 4;

            appendJsonCommand(json, emitted,
                String("{\"op\":\"scene\",\"scene_op\":") + String(op)
                + String(",\"a0\":") + String(a0)
                + String(",\"a1\":") + String(a1)
                + String(",\"a2\":") + String(a2)
                + String(",\"a3\":") + String(a3)
                + String(",\"a4\":") + String(a4)
                + String(",\"a5\":") + String(a5)
                + String(",\"a6\":") + String(a6)
                + String("}"));
        }
        appendJsonCommand(json, emitted,
            String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Orange Fortress 3D E3SB\",\"size\":30,\"r\":1.0,\"g\":0.95,\"b\":0.90}"));
        appendJsonCommand(json, emitted,
            String("{\"op\":\"text\",\"x\":56,\"y\":84,\"text\":")
            + JsonString(String("frame=") + String((int)frame_id)
                + String(" records=") + String((int)record_count)
                + String(" scene_ops=") + String(emitted), true).toString()
            + String(",\"size\":17,\"r\":0.82,\"g\":0.90,\"b\":0.78}"));

        json += String("]");
        {
            Mutex::Lock lock(render_lock);
            latest_surface_commands = json;
            latest_surface_valid = emitted;
            if (emitted) {
                scene_received = true;
                surface_revision++;
            }
        }
        printf("surface E3SB parsed: emitted=%d frame=%u records=%u\n",
               emitted, (unsigned)frame_id, (unsigned)record_count);
        traceLine(String("{\"event\":\"mailbox_e3sb_parsed\",\"emitted\":") + String(emitted)
            + String(",\"records\":") + String((int)record_count) + String("}"));
        traceKernelStateSnapshot("after_mailbox_e3sb");
        return true;
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
        uint32_t opcode = orangeFortressReadLeU32(bytes + offset);
        offset += 4;
        if (opcode == 255u) {
            break;
        }
        if (opcode == 1u) {
            if (offset + 28u > (size_t)msg_len) break;
            uint32_t x = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t y = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t w = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t h = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t r = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t g = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t b = orangeFortressReadLeU32(bytes + offset); offset += 4;
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
            uint32_t x0 = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t y0 = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t x1 = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t y1 = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t line_width = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t r = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t g = orangeFortressReadLeU32(bytes + offset); offset += 4;
            uint32_t b = orangeFortressReadLeU32(bytes + offset); offset += 4;
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
        json += String("{\"op\":\"text\",\"x\":56,\"y\":52,\"text\":\"Orange Fortress\",\"size\":32,\"r\":1.0,\"g\":0.95,\"b\":0.90},");
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
    return true;
}

String OrangeFortressApp::buildCachedCubeSceneJson(int angle) const {
    int emitted = 0;
    String json("[");
    int cam_x = 0;
    int cam_y = 650;
    int cam_z = -3000;
    int yaw = 0;
    int pitch = 2500;
    int cube_yaw = 0;
    String label("Front");

    if (angle == 1) {
        cam_x = 1800;
        cam_y = 900;
        cam_z = -2600;
        yaw = 35000;
        pitch = 7000;
        cube_yaw = 26000;
        label = String("Quarter");
    } else if (angle == 2) {
        cam_x = 0 - 2100;
        cam_y = 1250;
        cam_z = -2600;
        yaw = 0 - 40000;
        pitch = 13000;
        cube_yaw = 0 - 18000;
        label = String("High");
    }

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":20")
        + String(",\"a0\":18,\"a1\":20,\"a2\":24")
        + String(",\"a3\":44,\"a4\":42,\"a5\":40,\"a6\":0}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":10")
        + String(",\"a0\":") + String(cam_x)
        + String(",\"a1\":") + String(cam_y)
        + String(",\"a2\":") + String(cam_z)
        + String(",\"a3\":") + String(yaw)
        + String(",\"a4\":") + String(pitch)
        + String(",\"a5\":0")
        + String(",\"a6\":60000}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":11")
        + String(",\"a0\":80,\"a1\":12000,\"a2\":1000")
        + String(",\"a3\":0,\"a4\":0,\"a5\":0,\"a6\":0}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":30")
        + String(",\"a0\":1,\"a1\":255,\"a2\":132,\"a3\":22")
        + String(",\"a4\":0,\"a5\":720,\"a6\":0}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":40")
        + String(",\"a0\":1,\"a1\":0,\"a2\":0,\"a3\":36")
        + String(",\"a4\":0,\"a5\":1000,\"a6\":0}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":50")
        + String(",\"a0\":1,\"a1\":1,\"a2\":1")
        + String(",\"a3\":0,\"a4\":520,\"a5\":0,\"a6\":0}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"scene\",\"scene_op\":51")
        + String(",\"a0\":1")
        + String(",\"a1\":") + String(cube_yaw)
        + String(",\"a2\":0,\"a3\":0")
        + String(",\"a4\":760,\"a5\":760,\"a6\":760}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"text\",\"x\":42,\"y\":48,\"text\":")
        + JsonString(String("Orange Fortress cube angle ") + String(angle + 1) + String(" / 3"), true).toString()
        + String(",\"size\":28,\"r\":1.0,\"g\":0.94,\"b\":0.86}"));

    appendJsonCommand(json, emitted,
        String("{\"op\":\"text\",\"x\":42,\"y\":82,\"text\":")
        + JsonString(String("Camera: ") + label + String("    keys: 1 2 3, arrows, space"), true).toString()
        + String(",\"size\":17,\"r\":0.78,\"g\":0.88,\"b\":0.92}"));

    json += String("]");
    return json;
}

String OrangeFortressApp::buildSurfaceCommandsJson() const {
    Mutex::Lock lock(render_lock);
    if (latest_surface_valid) {
        return latest_surface_commands;
    }
    return String("[]");
}

String OrangeFortressApp::buildStatusItemsJson() const {
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

    String kernel_count_label = String("EPA debug VM: ") + (epa_started ? String("connected") : String("not connected"));
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

void OrangeFortressApp::buildDocument(ElaraUiDocumentBuilder &ui) {
    refreshProjectState();

    ui.clear();
    ui.createWindow(String("Orange Fortress"), 1480, 920, String("org.elara.ui.orange-fortress"));
    ui.setThemeMode(String("dark"));
    ui.setRootContent(String("app.surface"));

    ui.createWidget(String("app.surface"), String("elara.widgets.vulkan_surface"));
    ui.setPropertyString(String("app.surface"), String("backend"), String("vulkan"));
    ui.setPropertyString(String("app.surface"), String("kernel_name"), String("orange.fortress.scene"));
    ui.setPropertyString(String("app.surface"), String("overlay_text"), String("Orange Fortress"));
    ui.setPropertyNumber(String("app.surface"), String("virtual_width"), 1280);
    ui.setPropertyNumber(String("app.surface"), String("virtual_height"), 720);
    ui.setSectionJson(String("app.surface"), String("commands"), buildSurfaceCommandsJson());
}

bool OrangeFortressApp::loadDocument(const String &document_json) {
    String params = String("{\"document\":") + JsonString(document_json, true).toString() + String("}");
    String result_json;
    String error_code;
    String error_message;
    if (!peer->call(String("ui.loadDocument"), params, result_json, error_code, error_message, 5000)) {
        printf("ui.loadDocument failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
        failIfUiDisconnected("ui.loadDocument");
        return false;
    }
    printf("Document loaded: %s\n", result_json.operator char *());
    return true;
}

bool OrangeFortressApp::setSectionJson(const String &target, const String &section, const String &value_json) {
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
        failIfUiDisconnected("ui.setSectionJson");
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

bool OrangeFortressApp::pushUiState() {
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

bool OrangeFortressApp::printSnapshot() {
    String result_json;
    String error_code;
    String error_message;
    if (peer->call(String("ui.snapshot"), String("{}"), result_json, error_code, error_message, 5000)) {
        printf("%s\n", result_json.operator char *());
        return true;
    }
    printf("ui.snapshot failed [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
    failIfUiDisconnected("ui.snapshot");
    return false;
}

int OrangeFortressApp::run() {
    openTraceArtifact();

    peer->addService(Ref<sockets::rpc::json::JsonRPCService>(new UiEventSinkService(this)));

    if (!connectUiPeer()) {
        shutdown();
        return 1;
    }

    startExtLogicServer();
    sendHostDebugEvent(
        String("register"),
        String("\"pid\":") + String((int)getpid())
            + String(",\"message\":") + json_quote_simple(String("host connected to IDE debug bridge"))
    );
    sendHostDebugState(String("Status: waiting for bundle load"));

    ElaraUiDocumentBuilder ui;
    buildDocument(ui);
    bool ui_connected = loadDocument(ui.toJson());
    if (!ui_connected) {
        printf("[C++ Host] UI not available - running in headless debug mode\n");
        peer->close();
    } else {
        armUiInputFocus();
        traceLine(String("{\"event\":\"ui_connected\",\"host\":") + JsonString(host, true).toString()
            + String(",\"port\":") + String(port) + String("}"));
    }

    if (ui_connected) {
        sendHostDebugLog(String("connected to UI RPC head at ") + host + String(":") + String(port));
    }
    sendHostDebugLog(String("bundle path: ") + bundle_path);
    if (debug_session.enabled) {
        sendHostDebugLog(String("debug session id: ") + debug_session.session_id);
        sendHostDebugLog(String("debug session file: ") + debug_session.session_path);
        sendHostDebugLog(String("epa-dbg endpoint: ")
            + debug_session.epa_dbg_host
            + String(":")
            + String(debug_session.epa_dbg_port));
    }
    publishCachedCubeScene(0);

    if (!epaDbgLoadBundle()) {
        printf("[C++ Host] EPA VM not ready; running in cached-replay-only mode.\n");
    } else {
        sendScenePose();
    }
    traceLine(String("{\"event\":\"initial_scene_ingress\"}"));
    int loop_count = 0;
    bool cached_replay_started = false;
    bool stdin_open = true;
    while (true) {
        if (ui_quit_requested.load()) {
            printf("[C++ Host] UI server sent quit — shutting down cleanly.\n");
            traceLine(String("{\"event\":\"ui_quit_received\"}"));
            sendHostDebugLog(String("UI server quit received; host shutting down"));
            shutdown();
            return 0;
        }
        if (ui_connected && failIfUiDisconnected("main_loop")) {
            break;
        }
        fd_set readfds;
        struct timeval tv;
        int max_fd = -1;
        FD_ZERO(&readfds);
        if (stdin_open) {
            FD_SET(STDIN_FILENO, &readfds);
            max_fd = STDIN_FILENO;
        }
        tv.tv_sec = 0;
        tv.tv_usec = 33000;
        int rc = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (rc > 0 && stdin_open && FD_ISSET(STDIN_FILENO, &readfds)) {
            char line[256];
            if (!fgets(line, sizeof(line), stdin)) {
                stdin_open = false;
            } else {
                String command(line);
                command = command.trim();
                if (command == String("quit") || command == String("exit") || command == String("q")) {
                    shutdown();
                    return 0;
                }
                if (command == String("snapshot")) { printSnapshot(); }
            }
        }

        drainKeyEvents();

        if(ui_connected && !pushUiState()) {
            break;
        }
        bool should_start_cached_replay = false;
        {
            Mutex::Lock lock(render_lock);
            if (scene_received && !cached_replay_started) {
                cached_replay_started = true;
                scene_received = false;
                should_start_cached_replay = true;
            }
        }
        if (should_start_cached_replay) {
            printf("Scene confirm received from EPA scene worker.\n");
            traceLine(String("{\"event\":\"scene_confirm_received\"}"));
        }
        loop_count++;
        if (!cached_replay_started && loop_count > 150) {
            printf("Timed out waiting for EPA scene worker confirmation; entering cached cube replay anyway.\n");
            traceKernelStateSnapshot("scene_confirm_timeout");
            traceLine(String("{\"event\":\"scene_confirm_timeout_cached_replay\"}"));
            cached_replay_started = true;
            publishCachedCubeScene(cached_scene_angle);
        }
    }

    shutdown();
    return 1;
}

}

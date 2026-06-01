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
#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonString.h>
#include <libelarasockets/rpc/brpc/BRpcCodec.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>
#include <libelarauirpc/ElaraUiDocumentBuilder.h>
#include "OrangeFortressEpaDebugShim.h"

namespace elara {
using namespace elara::ui::rpc;
using sockets::rpc::brpc::BRpcReader;
using sockets::rpc::brpc::BRpcWriter;
using sockets::rpc::brpc::BRPC_ARRAY;
using sockets::rpc::brpc::BRPC_NAMED_BYTE;
using sockets::rpc::brpc::BRPC_NAMED_STRING;

namespace {

static OrangeFortressApp *g_orange_fortress_app = NULL;

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

static void elg_dispatch_frame(int fd, const std::vector<char> &frame) {
    BRpcReader reader(frame.data(), frame.size());
    uint8_t type;
    if (!reader.peekType(type) || type != BRPC_ARRAY) return;
    uint32_t total, count;
    if (!reader.readArrayHeader(total, count)) return;

    String id, method;
    for (uint32_t i = 0; i < count; i++) {
        if (!reader.peekType(type)) break;
        if (type == BRPC_NAMED_STRING) {
            String name, value;
            if (!reader.readNamedString(name, value)) break;
            if (name == String("id")) id = value;
            else if (name == String("method")) method = value;
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
            }

            result_json = "{\"received\":true}";
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
    if (g_orange_fortress_app) {
        g_orange_fortress_app->updateSurfaceCommandsFromMailbox((unsigned int)wid, msg, msg_len);
    }
    return 1;
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
        execlp("elaraui-server", "elaraui-server",
               "--port", port_text,
               "--backend-id", "org.elara.ui.orange-fortress",
               "--persistent",
               (char *)NULL);
        fprintf(stderr, "Failed to exec elaraui-server: %s\n", strerror(errno));
        _exit(127);
    }

    owned_ui_server_pid = pid;
    recordLaunchedPid("orange-fortress-ui-head", pid);
    host = String("127.0.0.1");
    port = fallback_port;
    printf("Spawned elaraui-server pid=%d on fallback port %d\n", (int)pid, port);

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
            elg_dispatch_frame(client, frame);
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
    size_t i;
    if (!epa_loaded) {
        traceLine(String("{\"event\":\"kernel_snapshot\",\"phase\":")
            + JsonString(String(phase ? phase : "unknown"), true).toString()
            + String(",\"ready\":false}"));
        return;
    }
    for (i = 0; i < epa.kernelCount(); i++) {
        EpaKernel *kernel = epa.rawKernelAt(i);
        OrangeFortressEpaDebugKernelSnapshot kernel_snapshot;
        OrangeFortressEpaDebugWorkerSnapshot workers[ORANGEFORTRESS_EPA_DEBUG_MAX_WORKERS];
        size_t worker_count = 0;
        String line;
        if (!kernel) {
            continue;
        }
        memset(&kernel_snapshot, 0, sizeof(kernel_snapshot));
        OrangeFortress_epa_debug_capture_kernel(kernel, &kernel_snapshot);
        worker_count = OrangeFortress_epa_debug_capture_workers(kernel, workers, ORANGEFORTRESS_EPA_DEBUG_MAX_WORKERS);
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

void OrangeFortressApp::refreshProjectState() {
    bundle_exists = access(bundle_path.operator char *(), F_OK) == 0;
}

void OrangeFortressApp::refreshEpaState() {
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
        scene_received = false;
        surface_revision = 0;
        pushed_surface_revision = 0;
    }
    traceLine(String("{\"event\":\"refresh_epa_state\",\"bundle_exists\":") + String(bundle_exists ? "true" : "false") + String("}"));
    if (!bundle_exists) {
        sendHostDebugState(String("Status: bundle missing"));
        return;
    }
    if (!epa.loadBundlePath(bundle_path)) {
        traceLine(String("{\"event\":\"bundle_load_failed\",\"error\":") + JsonString(epa.lastError(), true).toString() + String("}"));
        sendHostDebugState(String("Status: bundle load failed"));
        return;
    }
    epa_loaded = true;
    traceLine(String("{\"event\":\"bundle_loaded\",\"kernel_count\":") + String((int)epa.kernelCount()) + String("}"));
    installSurfaceCallback();
    if (!epa.startAllKernels()) {
        traceLine(String("{\"event\":\"start_all_failed\",\"error\":") + JsonString(epa.lastError(), true).toString() + String("}"));
        sendHostDebugState(String("Status: kernel start failed"));
        return;
    }
    epa_started = true;
    sendHostDebugState(String("Status: bundle loaded and kernels running"));
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

void OrangeFortressApp::stimulateEpa() {
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

bool OrangeFortressApp::sendScenePose() {
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

void OrangeFortressApp::enqueueKeyDown(unsigned int keyval) {
    Mutex::Lock lock(input_lock);
    pending_keydowns.push(keyval);
}

void OrangeFortressApp::updateKeyState(unsigned int keyval, bool pressed) {
    if (pressed) {
        if (keyval == 49u) { printf("keyboard angle hotkey: 1\n"); fflush(stdout); publishCachedCubeScene(0); return; }
        if (keyval == 50u) { printf("keyboard angle hotkey: 2\n"); fflush(stdout); publishCachedCubeScene(1); return; }
        if (keyval == 51u) { printf("keyboard angle hotkey: 3\n"); fflush(stdout); publishCachedCubeScene(2); return; }
        if (keyval == 32u) { printf("keyboard angle hotkey: next\n"); fflush(stdout); cycleCachedCubeScene(1); return; }
    }

    Mutex::Lock lock(input_lock);
    printf("updateKeyState: keyval=%u pressed=%d held_f=%d held_b=%d held_l=%d held_r=%d\n",
           keyval, (int)pressed, (int)held_forward, (int)held_back, (int)held_left, (int)held_right);
    fflush(stdout);
    // Arrow keys and WASD both control movement.
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

void OrangeFortressApp::handleMouseDown(int button, double x, double y) {
    (void)x;
    (void)y;
    printf("handleMouseDown: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1) {
        mouse_capture_requested = true;
    }
}

void OrangeFortressApp::handleMouseUp(int button, double x, double y) {
    printf("handleMouseUp: button=%d x=%.2f y=%.2f\n", button, x, y);
    if (button == 1 && !mouse_captured) {
        mouse_capture_requested = true;
    }
}

void OrangeFortressApp::handleMouseMove(double x, double y) {
    if (!mouse_captured) {
        return;
    }
    accumulateMouseDelta((int)x, (int)y);
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

void OrangeFortressApp::cycleCachedCubeScene(int delta) {
    int next_angle;
    {
        Mutex::Lock lock(render_lock);
        next_angle = cached_scene_angle + delta;
    }
    if (next_angle < 0) {
        next_angle = 2;
    }
    if (next_angle > 2) {
        next_angle = 0;
    }
    publishCachedCubeScene(next_angle);
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

void OrangeFortressApp::installSurfaceCallback() {
    const char *kernel_names[] = { "scene", "scene_compiler" };
    bool installed = false;
    g_orange_fortress_app = this;
    for (size_t i = 0; i < sizeof(kernel_names) / sizeof(kernel_names[0]); i++) {
        int idx = epa.findKernelIndex(String(kernel_names[i]));
        if (idx >= 0) {
            EpaKernel *kernel = epa.rawKernelAt((size_t)idx);
            if (kernel) {
                epa_kernel_set_signal_callback(kernel, on_surface_host_signal);
                installed = true;
                printf("Installed surface callback on kernel %s idx=%d\n", kernel_names[i], idx);
                traceLine(String("{\"event\":\"surface_callback_installed\",\"kernel\":")
                    + JsonString(String(kernel_names[i]), true).toString()
                    + String(",\"index\":") + String(idx) + String("}"));
            }
        }
    }
    if (!installed) {
        printf("Surface callback install failed: scene/scene_compiler kernels not found\n");
        traceLine(String("{\"event\":\"surface_callback_install_failed\",\"reason\":\"kernels_not_found\"}"));
    }
}

void OrangeFortressApp::shutdown() {
    g_orange_fortress_app = NULL;

    const char *kernel_names[] = { "scene", "scene_compiler" };
    for (size_t i = 0; i < sizeof(kernel_names) / sizeof(kernel_names[0]); i++) {
        int idx = epa.findKernelIndex(String(kernel_names[i]));
        if (idx >= 0) {
            EpaKernel *kernel = epa.rawKernelAt((size_t)idx);
            if (kernel) {
                epa_kernel_set_signal_callback(kernel, NULL);
            }
        }
    }

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

    epa.stopAllKernels();
    epa.destroy();
    closeTraceArtifact();
}

void OrangeFortressApp::updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len) {
    const unsigned char *bytes = (const unsigned char *)msg;
    size_t offset = 0;
    String json("[");
    String rect_summary("");
    int emitted = 0;
    int rect_count = 0;
    printf("surface mailbox callback wid=%u len=%d\n", wid, msg_len);
    traceLine(String("{\"event\":\"mailbox_callback\",\"wid\":") + String((int)wid) + String(",\"len\":") + String(msg_len) + String("}"));
    if ((wid != 1u && wid != 2u) || !msg || msg_len < 28) {
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

    refreshEpaState();
    if (!epa_started) {
        String error_text(epa.lastError());
        printf("EPA did not start: %s\n", error_text.operator char *());
        shutdown();
        return 1;
    }
    if (!sendScenePose()) {
        String error_text(epa.lastError());
        printf("Initial scene ingress failed: %s\n", error_text.operator char *());
        shutdown();
        return 1;
    }
    traceLine(String("{\"event\":\"initial_scene_ingress\"}"));
    int loop_count = 0;
    bool cached_replay_started = false;
    bool stdin_open = true;
    while (true) {
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
                if (command == String("1")) { publishCachedCubeScene(0); }
                else if (command == String("2")) { publishCachedCubeScene(1); }
                else if (command == String("3")) { publishCachedCubeScene(2); }
                else if (command == String("next")) { cycleCachedCubeScene(1); }
                else if (command == String("prev")) { cycleCachedCubeScene(-1); }
                else if (command == String("snapshot")) { printSnapshot(); }
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
            publishCachedCubeScene(cached_scene_angle);
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

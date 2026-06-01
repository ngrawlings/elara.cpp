#ifndef ORANGEFORTRESSAPP_H
#define ORANGEFORTRESSAPP_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarathreads/Mutex.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>
#include <stdio.h>
#include <mutex>
#include <thread>
#include "OrangeFortressEpaVmHost.h"

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

struct OrangeFortressDebugSessionConfig {
    bool enabled;
    String session_path;
    String session_id;
    String ui_rpc_host;
    int ui_rpc_port;
    String bundle_path;
    String epa_dbg_host;
    int epa_dbg_port;
    String host_debug_host;
    int host_debug_port;

    OrangeFortressDebugSessionConfig()
        : enabled(false),
          ui_rpc_port(18777),
          epa_dbg_port(0),
          host_debug_port(0) {
    }
};

class OrangeFortressApp {
public:
    OrangeFortressApp(const String &host, int port, const OrangeFortressDebugSessionConfig &debug_session = OrangeFortressDebugSessionConfig());
    ~OrangeFortressApp();
    int run();
    void enqueueKeyDown(unsigned int keyval);
    void updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len);
    void updateKeyState(unsigned int keyval, bool pressed);
    void accumulateMouseDelta(int dx, int dy);
    void handleMouseDown(int button, double x, double y);
    void handleMouseUp(int button, double x, double y);
    void handleMouseMove(double x, double y);

private:
    String host;
    int port;
    String bundle_path;
    bool bundle_exists;
    bool epa_loaded;
    bool epa_started;
    bool incremental_ui_supported;
    bool last_section_update_timed_out;
    OrangeFortressEpaVmHost epa;
    Mutex input_lock;
    mutable Mutex render_lock;
    Array<unsigned int> pending_keydowns;
    bool held_forward;
    bool held_back;
    bool held_left;
    bool held_right;
    int pending_mouse_dx;
    int pending_mouse_dy;
    bool mouse_captured;
    bool mouse_capture_requested;
    bool mouse_uncapture_requested;
    int scene_cam_x;
    int scene_cam_y;
    int scene_cam_z;
    int scene_cam_yaw;
    int scene_cam_pitch;
    int scene_depth;
    int scene_lane;
    int cached_scene_angle;
    String latest_surface_commands;
    bool latest_surface_valid;
    bool scene_received;
    unsigned long surface_revision;
    unsigned long pushed_surface_revision;
    String trace_path;
    FILE *trace_file;
    unsigned long trace_sequence;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;
    OrangeFortressDebugSessionConfig debug_session;
    int host_debug_fd;
    std::mutex host_debug_io_mutex;
    int ext_logic_server_fd;
    std::thread ext_logic_thread;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool setSectionJson(const String &target, const String &section, const String &value_json);
    bool pushUiState();
    bool printSnapshot();
    void armUiInputFocus();
    void armMouseCapture();
    void setMouseCaptured(bool captured);
    void refreshProjectState();
    void refreshEpaState();
    void stimulateEpa();
    bool sendScenePose();
    void drainKeyEvents();
    void installSurfaceCallback();
    void publishCachedCubeScene(int angle);
    void cycleCachedCubeScene(int delta);
    String buildCachedCubeSceneJson(int angle) const;
    String buildSurfaceCommandsJson() const;
    String buildStatusItemsJson() const;
    void openTraceArtifact();
    void closeTraceArtifact();
    void traceLine(const String &json_line);
    void traceKernelStateSnapshot(const char *phase);
    void sendHostDebugEvent(const String &kind, const String &payload_json);
    void sendHostDebugLog(const String &message);
    void sendHostDebugState(const String &status);
    bool connectHostDebugBridge();
    void closeHostDebugBridge();
    void startHostDebugReader();
    void hostDebugReadLoop();
    void startExtLogicServer();
    void extLogicServe();
    void shutdown();
};

}

#endif

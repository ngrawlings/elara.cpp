#ifndef ORANGEEXTERMINATORAPP_H
#define ORANGEEXTERMINATORAPP_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarathreads/Mutex.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>
#include <stdio.h>
#include "OrangeExterminatorEpaVmHost.h"

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

class OrangeExterminatorApp {
public:
    OrangeExterminatorApp(const String &host, int port);
    int run();
    void enqueueKeyDown(unsigned int keyval);
    void updateSurfaceCommandsFromMailbox(unsigned int wid, const char *msg, int msg_len);

private:
    String host;
    int port;
    String bundle_path;
    bool bundle_exists;
    bool epa_loaded;
    bool epa_started;
    bool incremental_ui_supported;
    OrangeExterminatorEpaVmHost epa;
    Mutex input_lock;
    mutable Mutex render_lock;
    Array<unsigned int> pending_keydowns;
    String latest_surface_commands;
    bool latest_surface_valid;
    String trace_path;
    FILE *trace_file;
    unsigned long trace_sequence;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool setSectionJson(const String &target, const String &section, const String &value_json);
    bool pushUiState();
    bool printSnapshot();
    void armUiInputFocus();
    void armMouseCapture();
    void refreshProjectState();
    void refreshEpaState();
    void stimulateEpa();
    void drainKeyEvents();
    void installSurfaceCallback();
    String buildSurfaceCommandsJson() const;
    String buildStatusItemsJson() const;
    void openTraceArtifact();
    void closeTraceArtifact();
    void traceLine(const String &json_line);
    void traceKernelStateSnapshot(const char *phase);
};

}

#endif

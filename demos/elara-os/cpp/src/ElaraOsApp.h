#ifndef ELARAOSAPP_H
#define ELARAOSAPP_H

#include <atomic>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>
#include <mutex>
#include <string>
#include <thread>

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

class ElaraOsApp {
public:
    ElaraOsApp(const String &host, int port, const String &host_bridge_host, int host_bridge_port);
    ~ElaraOsApp();
    int run();

private:
    String host;
    int port;
    String host_bridge_host;
    int host_bridge_port;
    int host_bridge_fd;
    std::string virtual_drive_root;
    std::atomic<bool> host_bridge_running;
    std::thread host_bridge_thread;
    std::mutex host_bridge_mutex;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool printSnapshot();
    bool bootstrapVirtualDrives();
    bool ensureDirectoryPath(const std::string &path);
    bool ensureFileContents(const std::string &path, const std::string &contents);
    bool connectHostDebugBridge();
    void stopHostDebugBridge();
    void hostDebugBridgeLoop();
    bool sendHostDebugEvent(const char *kind, const char *payload);
};

}

#endif

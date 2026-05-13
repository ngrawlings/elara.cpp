#ifndef EPADEBUGSMOKEAPP_H
#define EPADEBUGSMOKEAPP_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

class EpaDebugSmokeApp {
public:
    EpaDebugSmokeApp(const String &host, int port);
    int run();

private:
    String host;
    int port;
    Ref<ui::rpc::ElaraUiRpcPeer> peer;

    void buildDocument(ui::rpc::ElaraUiDocumentBuilder &ui);
    bool loadDocument(const String &document_json);
    bool printSnapshot();
};

}

#endif

#ifndef ORANGEEXTERMINATORAPP_H
#define ORANGEEXTERMINATORAPP_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>

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

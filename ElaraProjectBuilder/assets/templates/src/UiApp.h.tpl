>>>>>>>>>>main>>>>CLASS_NAME>CLASS_NAME_UPPER
#ifndef %CLASS_NAME_UPPER%_H
#define %CLASS_NAME_UPPER%_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelarauirpc/ElaraUiRpcPeer.h>

namespace elara {
namespace ui {
namespace rpc {
    class ElaraUiDocumentBuilder;
}
}

class %CLASS_NAME% {
public:
    %CLASS_NAME%(const String &host, int port);
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
<<<<<<<<<<main

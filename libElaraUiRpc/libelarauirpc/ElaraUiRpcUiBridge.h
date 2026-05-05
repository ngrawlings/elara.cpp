#ifndef ELARA_UI_RPC_UI_BRIDGE_H
#define ELARA_UI_RPC_UI_BRIDGE_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>

#include <libelaraui/frontend/widgets/ElaraRootWidget.h>

#include "ElaraUiRpcPeer.h"

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcUiBridge {
public:
    ElaraUiRpcUiBridge(
        ElaraRootWidget* root_widget,
        Ref<ElaraUiRpcPeer> rpc_peer
    );

    virtual ~ElaraUiRpcUiBridge();

    Ref<ElaraUiRpcPeer> getPeer() const;
    ElaraRootWidget* getRootWidget() const;

    void setOutboundEventMethod(const String& method_name);
    String getOutboundEventMethod() const;

    bool flushOutboundEvents(int timeout_ms = 1000);

private:
    ElaraRootWidget* root;
    Ref<ElaraUiRpcPeer> peer;
    String outbound_event_method;
};

}
}
}

#endif

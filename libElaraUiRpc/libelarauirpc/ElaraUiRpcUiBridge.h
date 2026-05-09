#ifndef ELARA_UI_RPC_UI_BRIDGE_H
#define ELARA_UI_RPC_UI_BRIDGE_H

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>

#include <libelaraui/frontend/widgets/ElaraRootWidget.h>

#include "ElaraUiRpcPeer.h"

namespace elara {
namespace ui {
namespace rpc {

class ElaraUiRpcEventSink {
public:
    virtual void onOutboundEvent(
        const String& target,
        const String& action,
        const String& payload,
        int seq,
        long long ts_ms
    ) = 0;
    virtual ~ElaraUiRpcEventSink() {}
};

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

    void setEventSink(ElaraUiRpcEventSink* sink);

    bool flushOutboundEvents(int timeout_ms = 1000);

private:
    ElaraRootWidget* root;
    Ref<ElaraUiRpcPeer> peer;
    String outbound_event_method;
    ElaraUiRpcEventSink* event_sink;
    int seq;
};

}
}
}

#endif

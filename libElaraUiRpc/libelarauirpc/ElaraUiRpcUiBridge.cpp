#include "ElaraUiRpcUiBridge.h"

#include <libelaraui/frontend/ElaraOutboundEventQueue.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>

namespace elara {
namespace ui {
namespace rpc {

ElaraUiRpcUiBridge::ElaraUiRpcUiBridge(
    ElaraRootWidget* root_widget,
    Ref<ElaraUiRpcPeer> rpc_peer
) : root(root_widget),
    peer(rpc_peer),
    outbound_event_method("ui.event") {
}

ElaraUiRpcUiBridge::~ElaraUiRpcUiBridge() {
}

Ref<ElaraUiRpcPeer> ElaraUiRpcUiBridge::getPeer() const {
    return peer;
}

ElaraRootWidget* ElaraUiRpcUiBridge::getRootWidget() const {
    return root;
}

void ElaraUiRpcUiBridge::setOutboundEventMethod(const String& method_name) {
    outbound_event_method = method_name;
}

String ElaraUiRpcUiBridge::getOutboundEventMethod() const {
    return outbound_event_method;
}

bool ElaraUiRpcUiBridge::flushOutboundEvents(int timeout_ms) {
    ElaraOutboundEventQueue* queue = ElaraOutboundEventQueue::getInstance();

    if(!queue || !peer) {
        return false;
    }

    while(queue->hasEvents()) {
        Ref<ElaraOutboundEvent> event = queue->pop();

        if(!event) {
            continue;
        }

        Memory handle_memory = event->widget_handle.getHandle();
        String target((const char*)handle_memory.getPtr(), handle_memory.length());
        String payload = event->payload.length() > 0 ? event->payload : String("null");
        String params_json =
            String("{\"target\":\"") +
            sockets::rpc::json::JsonRPCCodec::escapeJsonString(target) +
            String("\",\"action\":\"") +
            sockets::rpc::json::JsonRPCCodec::escapeJsonString(event->action) +
            String("\",\"payload\":") +
            payload +
            String("}");

        if(!peer->notify(outbound_event_method, params_json, timeout_ms)) {
            return false;
        }
    }

    return true;
}

}
}
}

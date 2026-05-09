#include "ElaraUiRpcUiBridge.h"

#include <time.h>

#include <libelaraui/frontend/ElaraOutboundEventQueue.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>

namespace elara {
namespace ui {
namespace rpc {

namespace {
long long nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
}
}

ElaraUiRpcUiBridge::ElaraUiRpcUiBridge(
    ElaraRootWidget* root_widget,
    Ref<ElaraUiRpcPeer> rpc_peer
) : root(root_widget),
    peer(rpc_peer),
    outbound_event_method("ui.event"),
    event_sink(0),
    seq(0) {
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

void ElaraUiRpcUiBridge::setEventSink(ElaraUiRpcEventSink* sink) {
    event_sink = sink;
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
        int event_seq = seq++;
        long long ts = nowMs();

        if(event_sink) {
            event_sink->onOutboundEvent(target, event->action, payload, event_seq, ts);
        }

        String params_json;
        if(event_sink) {
            params_json =
                String("{\"target\":\"") +
                sockets::rpc::json::JsonRPCCodec::escapeJsonString(target) +
                String("\",\"action\":\"") +
                sockets::rpc::json::JsonRPCCodec::escapeJsonString(event->action) +
                String("\",\"payload\":") +
                payload +
                String(",\"_seq\":") + String(event_seq) +
                String(",\"_ts_ms\":") + String((int)ts) +
                String("}");
        } else {
            params_json =
                String("{\"target\":\"") +
                sockets::rpc::json::JsonRPCCodec::escapeJsonString(target) +
                String("\",\"action\":\"") +
                sockets::rpc::json::JsonRPCCodec::escapeJsonString(event->action) +
                String("\",\"payload\":") +
                payload +
                String("}");
        }

        if(!peer->notify(outbound_event_method, params_json, timeout_ms)) {
            return false;
        }
    }

    return true;
}

}
}
}

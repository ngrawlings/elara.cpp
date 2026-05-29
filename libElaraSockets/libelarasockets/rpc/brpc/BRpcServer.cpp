//
//  BRpcServer.cpp
//  libElaraSockets
//

#include "BRpcServer.h"
#include "BRpcRpcCodec.h"

#include <arpa/inet.h>
#include <string.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

namespace {

static bool extractFramedPayload(ByteArray &buffer, ByteArray &payload) {
    if (buffer.length() < 4)
        return false;
    const char *bytes = (const char *)buffer;
    uint32_t length = ((uint32_t)(bytes[0] & 0xFF) << 24)
        | ((uint32_t)(bytes[1] & 0xFF) << 16)
        | ((uint32_t)(bytes[2] & 0xFF) <<  8)
        |  (uint32_t)(bytes[3] & 0xFF);
    if (buffer.length() < (int)(4 + length))
        return false;
    payload = buffer.subBytes(4, (int)length);
    buffer  = buffer.subBytes((int)(4 + length));
    return true;
}

class BRpcServerConnection : public Socket {
public:
    BRpcServerConnection(int fd, json::JsonRPCRegistry *registry, CallbackInterface *cb)
        : Socket(fd, cb), pending_input(), registry(registry) {}

protected:
    virtual void onReceive() {
        while (available()) {
            Memory chunk = read((int)available());
            if (chunk.length())
                pending_input.append(chunk);
        }

        ByteArray payload;
        while (extractFramedPayload(pending_input, payload)) {
            String id, method, params_json, parse_error;
            if (!BRpcRpcCodec::parseRequest((const char *)payload, (size_t)payload.length(),
                                            id, method, params_json, parse_error)) {
                ByteArray err = BRpcRpcCodec::buildErrorResponse(
                    String(""), String("parse_error"), parse_error);
                send(BRpcRpcCodec::framePayload(err));
                continue;
            }

            String result_json, error_code, error_message;
            bool ok = registry->dispatchParsed(id, method, params_json,
                                               result_json, error_code, error_message);
            ByteArray response;
            if (ok) {
                response = BRpcRpcCodec::buildSuccessResponse(id, result_json);
            } else {
                response = BRpcRpcCodec::buildErrorResponse(id, error_code, error_message);
            }
            send(BRpcRpcCodec::framePayload(response));
        }
    }

    virtual void onWriteReady() {}

private:
    ByteArray               pending_input;
    json::JsonRPCRegistry  *registry;
};

}  // namespace

BRpcServer::BRpcServer() : connections_lock("brpc-server-connections") {}

BRpcServer::~BRpcServer() {}

void BRpcServer::addService(Ref<json::JsonRPCService> service) {
    registry.addService(service);
}

void BRpcServer::listen(String bind_address, unsigned short port, EventBase *event_base) {
    Address address(Address::ADDR, (char *)bind_address);

    if (address.getType() == Address::IPV4) {
        unsigned int iface = 0;
        memcpy(&iface, address.getAddr(), 4);
        Listener::listen((int)port, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED,
                         iface, &in6addr_any, event_base);
        return;
    }

    if (address.getType() == Address::IPV6) {
        in6_addr iface;
        memcpy(&iface, address.getAddr(), 16);
        Listener::listen((int)port, LISTENER_OPTS_IPV6 | LISTENER_OPTS_IPV6_REQUIRED,
                         INADDR_ANY, &iface, event_base);
        return;
    }

    throw "Unsupported bind address";
}

void BRpcServer::onNewConnection(EventBase *event_base, int fd,
                                  unsigned char *addr, int addr_sz) {
    (void)event_base;
    (void)addr;
    (void)addr_sz;
    Ref<Socket> conn = Ref<Socket>(new BRpcServerConnection(fd, &registry, this));
    Mutex::Lock lock(connections_lock);
    connections.push(conn);
}

void BRpcServer::onDestroyed(Socket *socket) {
    Mutex::Lock lock(connections_lock);
    for (unsigned int i = 0; i < connections.length(); i++) {
        if (connections[i].getPtr() == socket) {
            connections.remove((int)i);
            break;
        }
    }
}

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara

#include "JsonRPCServer.h"

#include <arpa/inet.h>

#include "JsonRPCCodec.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    namespace {
        class JsonRPCServerConnection : public Socket {
        public:
            JsonRPCServerConnection(int fd, JsonRPCRegistry *registry, CallbackInterface *cb)
                : Socket(fd, cb), pending_input(), registry(registry) {
            }

        protected:
            virtual void onReceive() {
                while (available()) {
                    Memory chunk = read((int)available());
                    if (chunk.length())
                        pending_input.append(chunk);
                }

                String payload;
                while (JsonRPCCodec::extractFramedPayload(pending_input, payload)) {
                    String response;
                    registry->dispatch(payload, response);
                    ByteArray frame = JsonRPCCodec::framePayload(response);
                    send(frame);
                }
            }

            virtual void onWriteReady() {
            }

        private:
            ByteArray pending_input;
            JsonRPCRegistry *registry;
        };
    }

    JsonRPCServer::JsonRPCServer() : connections_lock("json-rpc-server-connections") {
    }

    JsonRPCServer::~JsonRPCServer() {
    }

    void JsonRPCServer::addService(Ref<JsonRPCService> service) {
        registry.addService(service);
    }

    void JsonRPCServer::listen(String bind_address, unsigned short port, EventBase *event_base) {
        Address address(Address::ADDR, (char*)bind_address);

        if (address.getType() == Address::IPV4) {
            unsigned int interface = 0;
            memcpy(&interface, address.getAddr(), 4);
            Listener::listen((int)port, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED, interface, &in6addr_any, event_base);
            return;
        }

        if (address.getType() == Address::IPV6) {
            in6_addr interface;
            memcpy(&interface, address.getAddr(), 16);
            Listener::listen((int)port, LISTENER_OPTS_IPV6 | LISTENER_OPTS_IPV6_REQUIRED, INADDR_ANY, &interface, event_base);
            return;
        }

        throw "Unsupported bind address";
    }

    void JsonRPCServer::onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {
        (void)event_base;
        (void)addr;
        (void)addr_sz;

        Ref<Socket> connection = Ref<Socket>(new JsonRPCServerConnection(fd, &registry, this));
        Mutex::Lock lock(connections_lock);
        connections.push(connection);
    }

    void JsonRPCServer::onDestroyed(Socket *socket) {
        Mutex::Lock lock(connections_lock);

        for (unsigned int i=0; i<connections.length(); i++) {
            if (connections[i].getPtr() == socket) {
                connections.remove((int)i);
                break;
            }
        }
    }

}
}
}
}

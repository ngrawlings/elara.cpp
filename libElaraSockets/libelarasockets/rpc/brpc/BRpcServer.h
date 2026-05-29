#ifndef ElaraSockets_BRpcServer_h
#define ElaraSockets_BRpcServer_h

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <libelarathreads/Mutex.h>

#include <libelarasockets/Listener.h>
#include <libelarasockets/Socket.h>

#include <libelarasockets/rpc/json/JsonRPCRegistry.h>
#include <libelarasockets/rpc/json/JsonRPCService.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

class BRpcServer : public Listener, protected Socket::CallbackInterface {
public:
    BRpcServer();
    virtual ~BRpcServer();

    void addService(Ref<json::JsonRPCService> service);
    void listen(String bind_address, unsigned short port, EventBase *event_base=0);

protected:
    virtual void onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
    virtual void onDestroyed(Socket *socket);

private:
    json::JsonRPCRegistry registry;
    Array< Ref<Socket> > connections;
    Mutex connections_lock;
};

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara

#endif  // ElaraSockets_BRpcServer_h

#ifndef ElaraSockets_JsonRPCServer_h
#define ElaraSockets_JsonRPCServer_h

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

#include <libelarasockets/Listener.h>
#include <libelarasockets/Socket.h>

#include "JsonRPCRegistry.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    class JsonRPCServer : public Listener, protected Socket::CallbackInterface {
    public:
        JsonRPCServer();
        virtual ~JsonRPCServer();

        void addService(Ref<JsonRPCService> service);
        void listen(String bind_address, unsigned short port, EventBase *event_base=0);

    protected:
        virtual void onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
        virtual void onDestroyed(Socket *socket);

    private:
        JsonRPCRegistry registry;
        Array< Ref<Socket> > connections;
        Mutex connections_lock;
    };

}
}
}
}

#endif

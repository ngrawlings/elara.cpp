#ifndef ElaraSockets_JsonRPCRegistry_h
#define ElaraSockets_JsonRPCRegistry_h

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>
#include <libelarathreads/Mutex.h>

#include "JsonRPCService.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    class JsonRPCRegistry {
    public:
        JsonRPCRegistry();
        virtual ~JsonRPCRegistry();

        void addService(Ref<JsonRPCService> service);
        bool dispatch(const String &request_json, String &response_json);
        void dispatchNotification(const String &notification_json);

    private:
        Mutex services_lock;
        Array< Ref<JsonRPCService> > services;

        Ref<JsonRPCService> findService(const String &service_name);
    };

}
}
}
}

#endif

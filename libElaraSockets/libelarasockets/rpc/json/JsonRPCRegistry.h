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

        // Pre-parsed variants for use with binary codecs (skips JSON parsing overhead).
        bool dispatchParsed(
            const String& id,
            const String& method,
            const String& params_json,
            String& result_json,
            String& error_code,
            String& error_message
        );
        void dispatchNotificationParsed(const String& method, const String& params_json);

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

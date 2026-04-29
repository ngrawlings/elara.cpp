#ifndef ElaraSockets_JsonRPCService_h
#define ElaraSockets_JsonRPCService_h

#include <libelaracore/memory/String.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    class JsonRPCService {
    public:
        explicit JsonRPCService(const String &service_name);
        virtual ~JsonRPCService();

        String getServiceName() const;

        virtual bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) = 0;

    private:
        String service_name;
    };

}
}
}
}

#endif

#include "JsonRPCService.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    JsonRPCService::JsonRPCService(const String &service_name) {
        this->service_name = service_name;
    }

    JsonRPCService::~JsonRPCService() {
    }

    String JsonRPCService::getServiceName() const {
        return service_name;
    }

    void JsonRPCService::notify(const String &method, const String &params_json) {
        (void)method;
        (void)params_json;
    }

}
}
}
}

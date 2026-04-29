#include "JsonRPCRegistry.h"

#include "JsonRPCCodec.h"

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    JsonRPCRegistry::JsonRPCRegistry() : services_lock("json-rpc-registry") {
    }

    JsonRPCRegistry::~JsonRPCRegistry() {
    }

    void JsonRPCRegistry::addService(Ref<JsonRPCService> service) {
        Mutex::Lock lock(services_lock);
        services.push(service);
    }

    Ref<JsonRPCService> JsonRPCRegistry::findService(const String &service_name) {
        Mutex::Lock lock(services_lock);

        for (unsigned int i=0; i<services.length(); i++) {
            if (services[i].getPtr() && services[i]->getServiceName() == service_name)
                return services[i];
        }

        return Ref<JsonRPCService>();
    }

    bool JsonRPCRegistry::dispatch(const String &request_json, String &response_json) {
        String id;
        String method;
        String params_json;
        String parse_error;

        if (!JsonRPCCodec::parseRequest(request_json, id, method, params_json, parse_error)) {
            response_json = JsonRPCCodec::buildErrorResponse("invalid", "invalid_request", parse_error);
            return false;
        }

        int split = method.indexOf(".");
        if (split <= 0 || split >= method.length() - 1) {
            response_json = JsonRPCCodec::buildErrorResponse(id, "invalid_method", "Method must be in the form service.method");
            return false;
        }

        String service_name = method.substr(0, split);
        String service_method = method.substr(split + 1);
        Ref<JsonRPCService> service = findService(service_name);
        if (!service.getPtr()) {
            response_json = JsonRPCCodec::buildErrorResponse(id, "service_not_found", "No registered service handled the requested method");
            return false;
        }

        String result_json;
        String error_code;
        String error_message;
        if (!service->call(service_method, params_json, result_json, error_code, error_message)) {
            if (!error_code.length())
                error_code = "service_error";
            if (!error_message.length())
                error_message = "The service rejected the request";
            response_json = JsonRPCCodec::buildErrorResponse(id, error_code, error_message);
            return false;
        }

        response_json = JsonRPCCodec::buildSuccessResponse(id, result_json);
        return true;
    }

}
}
}
}

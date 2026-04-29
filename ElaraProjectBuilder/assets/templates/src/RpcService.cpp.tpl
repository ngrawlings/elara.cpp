#include "%RpcServiceName%.h"

#include <libelarasockets/rpc/json/JsonRPCCodec.h>
%IndexedDataStoreHeaders%
%IndexedDataStoreHelpers%%RpcServiceName%::%RpcServiceName%() : JsonRPCService("app") {
}

%RpcServiceName%::~%RpcServiceName%() {
}

%OpenIndexedDataStoreMethod%bool %RpcServiceName%::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("ping")) {
        result_json = String("{\"message\":\"pong\"}");
        return true;
    }
%StoreMethodHandlers%    error_code = String("unknown_method");
    error_message = String("Unsupported RPC method");
    return false;
}

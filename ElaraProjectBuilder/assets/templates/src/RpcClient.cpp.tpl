#include "%RpcClientName%.h"
#include <libelarasockets/rpc/json/JsonRPCCodec.h>

%RpcClientName%::%RpcClientName%() {
}

%RpcClientName%::~%RpcClientName%() {
}

bool %RpcClientName%::connectTo(const String &address, int port) {
    return rpc_client.connect(address, (unsigned short)port);
}

void %RpcClientName%::close() {
    rpc_client.close();
}

bool %RpcClientName%::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    return rpc_client.call(method, params_json, result_json, error_code, error_message);
}

bool %RpcClientName%::ping(String &result_json, String &error_code, String &error_message) {
    return rpc_client.call(String("app.ping"), String("{}"), result_json, error_code, error_message);
}

%IndexedDataStoreClientMethods%

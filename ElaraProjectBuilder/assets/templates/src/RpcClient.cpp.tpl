>>>>>>>>>>main>>>>RPC_CLIENT_NAME>INCLUDE_INDEXED_DATA_STORE
#include "%RPC_CLIENT_NAME%.h"
#include <libelarasockets/rpc/json/JsonRPCCodec.h>

%RPC_CLIENT_NAME%::%RPC_CLIENT_NAME%() {
}

%RPC_CLIENT_NAME%::~%RPC_CLIENT_NAME%() {
}

bool %RPC_CLIENT_NAME%::connectTo(const String &address, int port) {
    return rpc_client.connect(address, (unsigned short)port);
}

void %RPC_CLIENT_NAME%::close() {
    rpc_client.close();
}

bool %RPC_CLIENT_NAME%::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    return rpc_client.call(method, params_json, result_json, error_code, error_message);
}

bool %RPC_CLIENT_NAME%::ping(String &result_json, String &error_code, String &error_message) {
    return rpc_client.call(String("app.ping"), String("{}"), result_json, error_code, error_message);
}

@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcClient.cpp.indexed_data_store_client_methods>>>>%RPC_CLIENT_NAME%
<<<<<<<<<<main

>>>>>>>>>>indexed_data_store_client_methods>>>>RPC_CLIENT_NAME
bool %RPC_CLIENT_NAME%::storeInit(String &result_json, String &error_code, String &error_message) {
    return rpc_client.call(String("app.storeInit"), String("{}"), result_json, error_code, error_message);
}

bool %RPC_CLIENT_NAME%::storePut(const String &key, const String &value, String &result_json, String &error_code, String &error_message) {
    String params = String("{\"key\":\"") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(key) + String("\",\"value\":\"") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(value) + String("\"}");
    return rpc_client.call(String("app.storePut"), params, result_json, error_code, error_message);
}

bool %RPC_CLIENT_NAME%::storeGet(const String &key, String &result_json, String &error_code, String &error_message) {
    String params = String("{\"key\":\"") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(key) + String("\"}");
    return rpc_client.call(String("app.storeGet"), params, result_json, error_code, error_message);
}
<<<<<<<<<<indexed_data_store_client_methods

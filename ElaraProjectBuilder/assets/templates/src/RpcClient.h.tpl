>>>>>>>>>>main>>>>RPC_CLIENT_NAME>INCLUDE_INDEXED_DATA_STORE
#ifndef %RPC_CLIENT_NAME%_h
#define %RPC_CLIENT_NAME%_h

#include <libelarasockets/rpc/json/JsonRPCClient.h>
#include <libelaracore/memory/String.h>

using elara::String;

class %RPC_CLIENT_NAME% {
public:
    %RPC_CLIENT_NAME%();
    virtual ~%RPC_CLIENT_NAME%();
    bool connectTo(const String &address, int port);
    void close();
    bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);
    bool ping(String &result_json, String &error_code, String &error_message);
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcClient.h.indexed_data_store_methods>>>>
private:
    elara::sockets::rpc::json::JsonRPCClient rpc_client;
};

#endif
<<<<<<<<<<main

>>>>>>>>>>indexed_data_store_methods
    bool storeInit(String &result_json, String &error_code, String &error_message);
    bool storePut(const String &key, const String &value, String &result_json, String &error_code, String &error_message);
    bool storeGet(const String &key, String &result_json, String &error_code, String &error_message);
<<<<<<<<<<indexed_data_store_methods

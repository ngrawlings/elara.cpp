#ifndef %RpcClientName%_h
#define %RpcClientName%_h

#include <libelarasockets/rpc/json/JsonRPCClient.h>
#include <libelaracore/memory/String.h>

using elara::String;

class %RpcClientName% {
public:
    %RpcClientName%();
    virtual ~%RpcClientName%();
    bool connectTo(const String &address, int port);
    void close();
    bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);
    bool ping(String &result_json, String &error_code, String &error_message);
%IndexedDataStoreMethods%
private:
    elara::sockets::rpc::json::JsonRPCClient rpc_client;
};

#endif

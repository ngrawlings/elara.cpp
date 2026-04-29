#ifndef %RpcServiceName%_h
#define %RpcServiceName%_h

#include <libelarasockets/rpc/json/JsonRPCService.h>
%IndexedDataStoreIncludes%using elara::String;

class %RpcServiceName% : public elara::sockets::rpc::json::JsonRPCService {
public:
    %RpcServiceName%();
    virtual ~%RpcServiceName%();
    virtual bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);
%IndexedDataStorePrivate%};

#endif

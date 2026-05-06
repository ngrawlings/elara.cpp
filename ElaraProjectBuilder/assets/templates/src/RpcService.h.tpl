>>>>>>>>>>main>>>>RPC_SERVICE_NAME>INCLUDE_INDEXED_DATA_STORE
#ifndef %RPC_SERVICE_NAME%_h
#define %RPC_SERVICE_NAME%_h

#include <libelarasockets/rpc/json/JsonRPCService.h>
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.h.indexed_data_store_includes>>>>
using elara::String;

class %RPC_SERVICE_NAME% : public elara::sockets::rpc::json::JsonRPCService {
public:
    %RPC_SERVICE_NAME%();
    virtual ~%RPC_SERVICE_NAME%();
    virtual bool call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message);
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.h.indexed_data_store_private>>>>
};

#endif
<<<<<<<<<<main

>>>>>>>>>>indexed_data_store_includes
#include <libelaraio/IndexedDataStore.h>
#include <libelaracore/memory/Ref.h>
using elara::IndexedDataStore;
using elara::Memory;
using elara::Ref;
<<<<<<<<<<indexed_data_store_includes

>>>>>>>>>>indexed_data_store_private
private:
    Ref<IndexedDataStore> openIndexedDataStore(bool create_if_missing);
<<<<<<<<<<indexed_data_store_private

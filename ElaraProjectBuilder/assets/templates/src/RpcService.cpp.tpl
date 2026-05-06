>>>>>>>>>>main>>>>RPC_SERVICE_NAME>INCLUDE_INDEXED_DATA_STORE>INDEXED_DATA_STORE_PATH>INDEX_DATA_STORE_BANK_MAP_REDUNDANCY
#include "%RPC_SERVICE_NAME%.h"

#include <libelarasockets/rpc/json/JsonRPCCodec.h>
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.cpp.indexed_data_store_headers>>>>
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.cpp.indexed_data_store_helpers>>>>
%RPC_SERVICE_NAME%::%RPC_SERVICE_NAME%() : JsonRPCService("app") {
}

%RPC_SERVICE_NAME%::~%RPC_SERVICE_NAME%() {
}

@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.cpp.open_indexed_data_store_method>>>>%RPC_SERVICE_NAME%>%INDEXED_DATA_STORE_PATH%>%INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%
bool %RPC_SERVICE_NAME%::call(const String &method, const String &params_json, String &result_json, String &error_code, String &error_message) {
    if (method == String("ping")) {
        result_json = String("{\"message\":\"pong\"}");
        return true;
    }
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] RpcService.cpp.store_method_handlers>>>>
    error_code = String("unknown_method");
    error_message = String("Unsupported RPC method");
    return false;
}
<<<<<<<<<<main

>>>>>>>>>>indexed_data_store_headers
#include <sys/stat.h>
#include <sys/types.h>
<<<<<<<<<<indexed_data_store_headers

>>>>>>>>>>indexed_data_store_helpers
namespace {
    void ensureDirectoryPath(String path) {
        String current;
        for (int i=0; i<path.length(); i++) {
            char ch = path.operator char *()[i];
            current += String(ch);
            if (ch == '/') {
                mkdir(current.operator char *(), 0755);
            }
        }
    }

    String parentDirectory(String path) {
        int slash = -1;
        for (int i=0; i<path.length(); i++) {
            if (path.operator char *()[i] == '/') {
                slash = i;
            }
        }
        if (slash <= 0) {
            return String();
        }
        return path.substr(0, slash);
    }
}

<<<<<<<<<<indexed_data_store_helpers

>>>>>>>>>>open_indexed_data_store_method>>>>RPC_SERVICE_NAME>INDEXED_DATA_STORE_PATH>INDEX_DATA_STORE_BANK_MAP_REDUNDANCY
Ref<IndexedDataStore> %RPC_SERVICE_NAME%::openIndexedDataStore(bool create_if_missing) {
    String path("%INDEXED_DATA_STORE_PATH%");
    if (create_if_missing) {
        String directory = parentDirectory(path);
        if (directory.length()) {
            ensureDirectoryPath(directory + String("/"));
        }
        return Ref<IndexedDataStore>(new IndexedDataStore(path, %INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%));
    }
    return Ref<IndexedDataStore>(new IndexedDataStore(path));
}

<<<<<<<<<<open_indexed_data_store_method

>>>>>>>>>>store_method_handlers
    if (method == String("storeInit")) {
        try {
            openIndexedDataStore(true);
            result_json = String("{\"initialised\":true}");
            return true;
        } catch (const char *error) {
            error_code = String("store_init_failed");
            error_message = String(error);
            return false;
        }
    }
    if (method == String("storePut")) {
        String key;
        String value;
        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String("key"), key);
        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String("value"), value);
        if (!key.length()) {
            error_code = String("missing_key");
            error_message = String("key is required");
            return false;
        }
        try {
            Ref<IndexedDataStore> store = openIndexedDataStore(false);
            store->set(Memory((char*)key, key.length()), Memory((char*)value, value.length()));
            result_json = String("{\"stored\":true}");
            return true;
        } catch (const char *error) {
            error_code = String("store_put_failed");
            error_message = String(error);
            return false;
        }
    }
    if (method == String("storeGet")) {
        String key;
        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String("key"), key);
        if (!key.length()) {
            error_code = String("missing_key");
            error_message = String("key is required");
            return false;
        }
        try {
            Ref<IndexedDataStore> store = openIndexedDataStore(false);
            Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store->getFile(Memory((char*)key, key.length()));
            if (!file.getPtr()) {
                error_code = String("not_found");
                error_message = String("No value for the requested key");
                return false;
            }
            unsigned long long len = store->getFileSize(file);
            Memory value = store->readFromFile(file, 0, len);
            String escaped_value = elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(String((char*)value, value.length()));
            result_json = String("{\"value\":\"") + escaped_value + String("\"}");
            return true;
        } catch (const char *error) {
            error_code = String("store_get_failed");
            error_message = String(error);
            return false;
        }
    }
<<<<<<<<<<store_method_handlers

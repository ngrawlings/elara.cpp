>>>>>>>>>>include_indexed_data_store
#include <libelaracore/memory/ByteArray.h>
#include <libelaraio/IndexedDataStore.h>
#include <sys/stat.h>
#include <sys/types.h>
<<<<<<<<<<include_indexed_data_store

>>>>>>>>>>include_thread_pool
#include <libelarathreads/Thread.h>
#include <libelarathreads/Task.h>
<<<<<<<<<<include_thread_pool

>>>>>>>>>>include_threaded_worker>>>>THREADED_WORKER_GENERATED_CLASS_NAME
#include <libelaracore/memory/Ref.h>
#include "%THREADED_WORKER_GENERATED_CLASS_NAME%.h"
<<<<<<<<<<include_threaded_worker

>>>>>>>>>>include_socket>>>>SOCKET_CLASS_NAME
#include <libelaraevent/EventBase.h>
#include <libelarasockets/Socket.h>
#include <unistd.h>
#include "%SOCKET_CLASS_NAME%.h"
<<<<<<<<<<include_socket

>>>>>>>>>>include_rpc_client
#include <libelaracore/parsing/CommandLineParser.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
<<<<<<<<<<include_rpc_client

>>>>>>>>>>indexed_data_store_helpers>>>>INDEXED_DATA_STORE_PATH>INDEX_DATA_STORE_BANK_MAP_REDUNDANCY
static void ensureDirectoryPath(String path) {
    String current;\n";
    for (int i=0; i<path.length(); i++) {
        char ch = path.operator char *()[i];
        current += String(ch);
        if (ch == '/') {
            mkdir(current.operator char *(), 0755);
        }
    }
}

static String parentDirectory(String path) {
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

static Ref<IndexedDataStore> openIndexedDataStore(bool create_if_missing) {
    String path("%INDEXED_DATA_STORE_PATH%");
    if (create_if_missing) {
        String directory = parentDirectory(path);
        if (directory.length()) {
            ensureDirectoryPath(directory + String(\"/\"));
        }
        return Ref<IndexedDataStore>(new IndexedDataStore(path, %INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%));
    }
    return Ref<IndexedDataStore>(new IndexedDataStore(path));
}
<<<<<<<<<<indexed_data_store_helpers
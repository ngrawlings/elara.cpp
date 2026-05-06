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
    String current; 
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
            ensureDirectoryPath(directory + String("/"));
        }
        return Ref<IndexedDataStore>(new IndexedDataStore(path, %INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%));
    }
    return Ref<IndexedDataStore>(new IndexedDataStore(path));
}
<<<<<<<<<<indexed_data_store_helpers


>>>>>>>>>>socket_client_rpc
        if (command == String("rpc profile")) {
            printf("RPC profile: %s\n", rpc_profile.operator char *());
            continue;
        }
        if (command.startsWith("rpc profile ")) {
            String requested_profile = command.substr(12).trim();
            if (requested_profile == String("rpc-default") || requested_profile == String("simple")) {
                rpc_profile = requested_profile;
                printf("RPC profile set to %s\n", rpc_profile.operator char *());
            } else {
                printf("Unsupported RPC profile: %s\n", requested_profile.operator char *());
            }
            continue;
        }
        if (command == String("rpc connect")) {
            socket_client->close();
            rpc_connected = socket_client->connectTo(rpc_address, rpc_port);
            if (rpc_connected) {
                printf("Connected JSON RPC client to %s:%d\n", rpc_address.operator char *(), rpc_port);
            } else {
                printf("Failed to connect JSON RPC client to %s:%d\n", rpc_address.operator char *(), rpc_port);
            }
            continue;
        }
        if (command.startsWith("rpc connect ")) {
            String endpoint = command.substr(12).trim();
            int split = -1;
            for (int i=0; i<endpoint.length(); i++) {
                if (endpoint.operator char *()[i] == ' ') {
                    split = i;
                    break;
                }
            }
            if (split == -1) {
                printf("Usage: rpc connect <address> <port>\n");
                continue;
            }
            String address = endpoint.substr(0, split).trim();
            String port_text = endpoint.substr(split + 1).trim();
            int port = atoi(port_text.operator char *());
            if (!address.length() || port <= 0 || port > 65535) {
                printf("Usage: rpc connect <address> <port>\n");
                continue;
            }
            socket_client->close();
            rpc_address = address;
            rpc_port = port;
            rpc_connected = socket_client->connectTo(rpc_address, rpc_port);
            if (rpc_connected) {
                printf("Connected JSON RPC client to %s:%d\n", rpc_address.operator char *(), rpc_port);
            } else {
                printf("Failed to connect JSON RPC client to %s:%d\n", rpc_address.operator char *(), rpc_port);
            }
            continue;
        }
        if (command == String("rpc disconnect")) {
            socket_client->close();
            rpc_connected = false;
            printf("JSON RPC client disconnected.\n");
            continue;
        }
        if (command.startsWith("rpc call ")) {
            String payload = command.substr(9).trim();
            int split = -1;
            for (int i=0; i<payload.length(); i++) {
                if (payload.operator char *()[i] == ' ') {
                    split = i;
                    break;
                }
            }
            String method = payload;
            String params_json("{}");
            if (split != -1) {
                method = payload.substr(0, split).trim();
                params_json = payload.substr(split + 1).trim();
                if (!params_json.length()) {
                    params_json = String("{}");
                }
            }
            if (!method.length()) {
                printf("Usage: rpc call <method> [params-json]\n");
                continue;
            }
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->call(method, params_json, result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                rpc_connected = false;
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
        if (command.startsWith("rpc invoke ")) {
            CommandLineInvocation invocation;
            String invocation_text = command.substr(11).trim();
            String parse_error;
            String params_json("[");
            String method;
            if (!CommandLineParser::parse(invocation_text, invocation, parse_error, rpc_profile)) {
                printf("RPC parse error: %s\n", parse_error.operator char *());
                continue;
            }
            method = invocation.getQualifiedMethod();
            for (unsigned int i=0; i<invocation.parameters.length(); i++) {
                if (i)
                    params_json += String(",");
                params_json += String("\"");
                params_json += elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(invocation.parameters[i]);
                params_json += String("\"");
            }
            params_json += String("]");
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->call(method, params_json, result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                rpc_connected = false;
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
        if (command == String("ping")) {
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->ping(result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                rpc_connected = false;
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
<<<<<<<<<<socket_client_rpc

>>>>>>>>>>indexed_data_store_cli_logic>>>>INDEXED_DATA_STORE_PATH
        if (command == String("initstore")) {
            try {
                openIndexedDataStore(true);
                printf("IndexedDataStore initialised at %INDEXED_DATA_STORE_PATH%.\n");
            } catch (const char *error) {
                printf("IndexedDataStore init failed: %s\n", error);
            }
            continue;
        }
        if (command.startsWith("put ")) {
            int split = -1;
            for (int i=4; i<command.length(); i++) {
                if (command.operator char *()[i] == ' ') {
                    split = i;
                    break;
                }
            }
            if (split == -1) {
                printf("Usage: put <key> <value>\n");
                continue;
            }
            String key = command.substr(4, split - 4);
            String value = command.substr(split + 1);
            try {
                Ref<IndexedDataStore> store = openIndexedDataStore(false);
                store->set(Memory((char*)key, key.length()), Memory((char*)value, value.length()));
                printf("Stored key '%s'.\n", key.operator char *());
            } catch (const char *error) {
                printf("IndexedDataStore put failed: %s\n", error);
            }
            continue;
        }
        if (command.startsWith("get ")) {
            String key = command.substr(4);
            try {
                Ref<IndexedDataStore> store = openIndexedDataStore(false);
                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store->getFile(Memory((char*)key, key.length()));
                if (!file.getPtr()) {
                    printf("No value for key '%s'.\n", key.operator char *());
                    continue;
                }
                unsigned long long len = store->getFileSize(file);
                Memory value = store->readFromFile(file, 0, len);
                printf("%s\n", String((char*)value, value.length()).operator char *());
            } catch (const char *error) {
                printf("IndexedDataStore get failed: %s\n", error);
            }
            continue;
        }
<<<<<<<<<<indexed_data_store_cli_logic

>>>>>>>>>>indexed_data_store_cli_logic_json_rpc
        if (command == String("remote-initstore")) {
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->storeInit(result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
        if (command.startsWith("remote-put ")) {
            int split = -1;
            for (int i=11; i<command.length(); i++) {
                if (command.operator char *()[i] == ' ') {
                    split = i;
                    break;
                }
            }
            if (split == -1) {
                printf("Usage: remote-put <key> <value>\n");
                continue;
            }
            String key = command.substr(11, split - 11);
            String value = command.substr(split + 1);
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->storePut(key, value, result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
        if (command.startsWith("remote-get ")) {
            String key = command.substr(11);
            String result_json;
            String error_code;
            String error_message;
            if (socket_client->storeGet(key, result_json, error_code, error_message)) {
                printf("%s\n", result_json.operator char *());
            } else {
                printf("RPC error [%s]: %s\n", error_code.operator char *(), error_message.operator char *());
            }
            continue;
        }
<<<<<<<<<<indexed_data_store_cli_logic_json_rpc

>>>>>>>>>>help_threaded_worker
    printf("  work <text> - queue the worker task\n");
<<<<<<<<<<help_threaded_worker

>>>>>>>>>>help_socket_server_status
    printf("  status - display thread pool state\n");
<<<<<<<<<<help_socket_server_status

>>>>>>>>>>help_socket_client_plain
    printf("  send <text> - send text to the remote socket\n");
<<<<<<<<<<help_socket_client_plain

>>>>>>>>>>help_socket_client_rpc
    printf("  rpc connect [address] [port] - connect the JSON RPC client\n");
    printf("  rpc call <method> [params-json] - call a JSON RPC method\n");
    printf("  rpc disconnect - close the JSON RPC client socket\n");
    printf("  rpc profile - show the current command-line profile\n");
    printf("  rpc profile <rpc-default|simple> - select the invocation profile\n");
    printf("  rpc invoke <command-line> - parse and invoke a remote RPC method\n");
    printf("  ping - call echo.ping on the remote RPC server\n");
<<<<<<<<<<help_socket_client_rpc

>>>>>>>>>>help_socket_client_rpc_store
    printf("  remote-initstore - call store.init on the remote RPC server\n");
    printf("  remote-put <key> <value> - call store.put on the remote RPC server\n");
    printf("  remote-get <key> - call store.get on the remote RPC server\n");
<<<<<<<<<<help_socket_client_rpc_store

>>>>>>>>>>help_indexed_data_store
    printf("  initstore - create or reset the IndexedDataStore file\n");
    printf("  put <key> <value> - persist a UTF-8 string value\n");
    printf("  get <key> - load and print a UTF-8 string value\n");
<<<<<<<<<<help_indexed_data_store

>>>>>>>>>>startup_thread_pool
    Thread::pool = true;
    Task::staticInit();
    Thread::init(4);
    printf("Thread pool initialised with 4 worker threads.\n\n");
<<<<<<<<<<startup_thread_pool

>>>>>>>>>>startup_socket_server_plain>>>>SOCKET_SERVER_NAME>SOCKET_PORT>SOCKET_ADDRESS
    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());
    Socket::init(event_base.getPtr());
    Ref<%SOCKET_SERVER_NAME%> socket_server = Ref<%SOCKET_SERVER_NAME%>(new %SOCKET_SERVER_NAME%());
    socket_server->start(%SOCKET_PORT%, String("%SOCKET_ADDRESS%"));
    printf("Socket server started on %SOCKET_ADDRESS%:%SOCKET_PORT%.\n\n");
<<<<<<<<<<startup_socket_server_plain

>>>>>>>>>>startup_socket_server_rpc>>>>RPC_SERVER_NAME>SOCKET_PORT>SOCKET_ADDRESS
    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());
    Socket::init(event_base.getPtr());
    Ref<%RPC_SERVER_NAME%> socket_server = Ref<%RPC_SERVER_NAME%>(new %RPC_SERVER_NAME%());
    socket_server->start(%SOCKET_PORT%, String("%SOCKET_ADDRESS%"));
    printf("JSON RPC server started on %SOCKET_ADDRESS%:%SOCKET_PORT%.\n\n");
<<<<<<<<<<startup_socket_server_rpc

>>>>>>>>>>startup_socket_client_plain>>>>SOCKET_CLIENT_NAME>SOCKET_PORT>SOCKET_ADDRESS
    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());
    Socket::init(event_base.getPtr());
    event_base->runEventLoop(true);
    Ref<%SOCKET_CLIENT_NAME%> socket_client = Ref<%SOCKET_CLIENT_NAME%>(new %SOCKET_CLIENT_NAME%(String("%SOCKET_ADDRESS%"), %SOCKET_PORT%));
    printf("Socket client connecting to %SOCKET_ADDRESS%:%SOCKET_PORT%.\n\n");
<<<<<<<<<<startup_socket_client_plain

>>>>>>>>>>startup_socket_client_rpc>>>>RPC_CLIENT_NAME>SOCKET_PORT>SOCKET_ADDRESS
    Ref<%RPC_CLIENT_NAME%> socket_client = Ref<%RPC_CLIENT_NAME%>(new %RPC_CLIENT_NAME%());
    String rpc_address("%SOCKET_ADDRESS%");
    int rpc_port = %SOCKET_PORT%;
    String rpc_profile("rpc-default");
    bool rpc_connected = false;
    if (!socket_client->connectTo(String("%SOCKET_ADDRESS%"), %SOCKET_PORT%)) {
        printf("Failed to connect JSON RPC client to %SOCKET_ADDRESS%:%SOCKET_PORT%.\n");
    } else {
        rpc_connected = true;
        printf("JSON RPC client connected to %SOCKET_ADDRESS%:%SOCKET_PORT%.\n");
    }

<<<<<<<<<<startup_socket_client_rpc

>>>>>>>>>>indexed_data_store_repl_banner>>>>INDEXED_DATA_STORE_PATH>INDEX_DATA_STORE_BANK_MAP_REDUNDANCY
        printf("IndexedDataStore path: %INDEXED_DATA_STORE_PATH% (bank-map redundancy %INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%)\n");
        printf("Run 'initstore' before using persistence commands on a new project.\n");
<<<<<<<<<<indexed_data_store_repl_banner

>>>>>>>>>>command_status
        if (command == String("status")) {
            int total = 0;
            int active = 0;
            Thread::getThreadPoolState(&total, &active);
            printf("Thread pool: total=%d active=%d waiting=%d\n", total, active, total - active);
            continue;
        }
<<<<<<<<<<command_status

>>>>>>>>>>command_send_plain
        if (command.startsWith("send ")) {
            socket_client->sendLine(command.substr(5));
            continue;
        }
<<<<<<<<<<command_send_plain

>>>>>>>>>>command_worker>>>>WORKER_NAME
        if (command.startsWith("work ")) {
            elara::threading::memory::Ref<Task> task = elara::threading::memory::Ref<Task>(new %WORKER_NAME%(command.substr(5)));
            Thread::runTask(task);
            printf("Queued worker task.\n");
            continue;
        }
<<<<<<<<<<command_worker

>>>>>>>>>>no_repl_server_loop
    printf("Server running. Press Ctrl+C to stop.\n");
    while (true) {
        sleep(1);
    }
<<<<<<<<<<no_repl_server_loop

>>>>>>>>>>no_repl_client_loop
    printf("Client running. Press Ctrl+C to stop.\n");
    while (true) {
        sleep(1);
    }
<<<<<<<<<<no_repl_client_loop

>>>>>>>>>>no_socket_message>>>>PROJECT_NAME
    printf("%PROJECT_NAME% generated successfully. Add your application logic here.\n");
<<<<<<<<<<no_socket_message

>>>>>>>>>>shutdown_socket_server
    socket_server->stop();
<<<<<<<<<<shutdown_socket_server

>>>>>>>>>>shutdown_socket_client_plain
    event_base->breakEventLoop();
    socket_client->close();
<<<<<<<<<<shutdown_socket_client_plain

>>>>>>>>>>shutdown_socket_client_rpc
    socket_client->close();
<<<<<<<<<<shutdown_socket_client_rpc

>>>>>>>>>>shutdown_thread_pool
    Thread::stopAllThreads();
    Thread::staticCleanUp();
    Task::staticCleanup();
<<<<<<<<<<shutdown_thread_pool

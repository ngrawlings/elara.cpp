>>>>>>>>>>main>>>>PROJECT_NAME>TARGET_NAME>INCLUDE_REPL>INCLUDE_THREAD_POOL>INCLUDE_THREADED_WORKER>WORKER_NAME>INCLUDE_SOCKET_SERVER>INCLUDE_SOCKET_CLIENT>INCLUDE_SOCKET_CLIENT_PLAIN>INCLUDE_SOCKET_SERVER_PLAIN>INCLUDE_SOCKET_SERVER_RPC>INCLUDE_SOCKET_CLIENT_RPC>INCLUDE_SOCKET_CLIENT_RPC_STORE>SOCKET_ADDRESS>SOCKET_PORT>SOCKET_SERVER_NAME>SOCKET_CLIENT_NAME>RPC_SERVER_NAME>RPC_CLIENT_NAME>INCLUDE_INDEXED_DATA_STORE>INDEXED_DATA_STORE_PATH>INDEX_DATA_STORE_BANK_MAP_REDUNDANCY
#include <stdio.h>
#include <string.h>
#include <libelaracore/memory/String.h>
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] main.cpp.blocks.include_indexed_data_store>>>>
@include [%INCLUDE_THREAD_POOL% == 1] main.cpp.blocks.include_thread_pool>>>>
@include [%INCLUDE_THREADED_WORKER% == 1] main.cpp.blocks.include_threaded_worker>>>>%WORKER_NAME%
@include [%INCLUDE_SOCKET_CLIENT_PLAIN% == 1] main.cpp.blocks.include_socket>>>>%SOCKET_CLIENT_NAME%
@include [%INCLUDE_SOCKET_SERVER_PLAIN% == 1] main.cpp.blocks.include_socket>>>>%SOCKET_SERVER_NAME%
@include [%INCLUDE_SOCKET_SERVER_RPC% == 1] main.cpp.blocks.include_socket>>>>%RPC_SERVER_NAME%
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.include_rpc_client>>>>
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.include_socket>>>>%RPC_CLIENT_NAME%

using namespace elara;

static void printHelp() {
    printf("Commands:\n");
    printf("  help  - show this help\n");
    printf("  quit  - exit the application\n");
@include [%INCLUDE_THREADED_WORKER% == 1] main.cpp.blocks.help_threaded_worker>>>>
@include [%INCLUDE_SOCKET_SERVER% == 1] main.cpp.blocks.help_socket_server_status>>>>
@include [%INCLUDE_SOCKET_CLIENT_PLAIN% == 1] main.cpp.blocks.help_socket_client_plain>>>>
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.help_socket_client_rpc>>>>
@include [%INCLUDE_SOCKET_CLIENT_RPC_STORE% == 1] main.cpp.blocks.help_socket_client_rpc_store>>>>
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] main.cpp.blocks.help_indexed_data_store>>>>
}

@include [%INCLUDE_INDEXED_DATA_STORE% == 1] main.cpp.blocks.indexed_data_store_helpers>>>>%INDEXED_DATA_STORE_PATH%>%INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%

int main() {

@include [%INCLUDE_THREAD_POOL% == 1] main.cpp.blocks.startup_thread_pool>>>>
@include [%INCLUDE_SOCKET_SERVER_PLAIN% == 1] main.cpp.blocks.startup_socket_server_plain>>>>%SOCKET_SERVER_NAME%>%SOCKET_PORT%>%SOCKET_ADDRESS%
@include [%INCLUDE_SOCKET_SERVER_RPC% == 1] main.cpp.blocks.startup_socket_server_rpc>>>>%RPC_SERVER_NAME%>%SOCKET_PORT%>%SOCKET_ADDRESS%
@include [%INCLUDE_SOCKET_CLIENT_PLAIN% == 1] main.cpp.blocks.startup_socket_client_plain>>>>%SOCKET_CLIENT_NAME%>%SOCKET_PORT%>%SOCKET_ADDRESS%
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.startup_socket_client_rpc>>>>%RPC_CLIENT_NAME%>%SOCKET_PORT%>%SOCKET_ADDRESS%

    if (%INCLUDE_REPL% == 1) {
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] main.cpp.blocks.indexed_data_store_repl_banner>>>>%INDEXED_DATA_STORE_PATH%>%INDEX_DATA_STORE_BANK_MAP_REDUNDANCY%
        printHelp();
        char line[1024];
        while (true) {
            printf("%TARGET_NAME%> ");
            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }
            String command = String(line);
            command = command.trim();
            if (!command.length()) {
                continue;
            }
            if (command == String("help")) {
                printHelp();
                continue;
            }
            if (command == String("quit") || command == String("exit")) {
                break;
            }
@include [%INCLUDE_SOCKET_SERVER% == 1] main.cpp.blocks.command_status>>>>
@include [%INCLUDE_SOCKET_CLIENT_PLAIN% == 1] main.cpp.blocks.command_send_plain>>>>
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.socket_client_rpc>>>>
@include [%INCLUDE_THREADED_WORKER% == 1] main.cpp.blocks.command_worker>>>>%WORKER_NAME%
@include [%INCLUDE_INDEXED_DATA_STORE% == 1] main.cpp.blocks.indexed_data_store_cli_logic>>>>%INDEXED_DATA_STORE_PATH%
@include [%INCLUDE_SOCKET_CLIENT_RPC_STORE% == 1] main.cpp.blocks.indexed_data_store_cli_logic_json_rpc>>>>
            printf("Unhandled command: %s\n", command.operator char *());
        }
    } else if (%INCLUDE_SOCKET_SERVER% == 1) {
@include [%INCLUDE_SOCKET_SERVER% == 1] main.cpp.blocks.no_repl_server_loop>>>>
    } else if (%INCLUDE_SOCKET_CLIENT% == 1) {
@include [%INCLUDE_SOCKET_CLIENT% == 1] main.cpp.blocks.no_repl_client_loop>>>>
    } else {
@include [%INCLUDE_SOCKET_CLIENT% == 0] main.cpp.blocks.no_socket_message>>>>%PROJECT_NAME%
    }

@include [%INCLUDE_SOCKET_SERVER% == 1] main.cpp.blocks.shutdown_socket_server>>>>
@include [%INCLUDE_SOCKET_CLIENT_PLAIN% == 1] main.cpp.blocks.shutdown_socket_client_plain>>>>
@include [%INCLUDE_SOCKET_CLIENT_RPC% == 1] main.cpp.blocks.shutdown_socket_client_rpc>>>>
@include [%INCLUDE_THREAD_POOL% == 1] main.cpp.blocks.shutdown_thread_pool>>>>
    return 0;
}
<<<<<<<<<<main

#ifndef ElaraProjectBuilder_ProjectOptions_h
#define ElaraProjectBuilder_ProjectOptions_h

#include <libelaracore/memory/String.h>

namespace elara {

    class ProjectOptions {
    public:
        enum SocketMode {
            SOCKET_DISABLED,
            SOCKET_SERVER,
            SOCKET_CLIENT
        };

        enum SocketTransport {
            SOCKET_TRANSPORT_PLAIN,
            SOCKET_TRANSPORT_JSON_RPC
        };

        ProjectOptions() :
            include_repl(true),
            socket_mode(SOCKET_DISABLED),
            socket_transport(SOCKET_TRANSPORT_PLAIN),
            include_debug_harness(true),
            include_thread_pool(false),
            include_threaded_worker(false),
            include_indexed_data_store(false),
            indexed_data_store_bank_map_redundancy(2),
            socket_port(4040) {
        }

        String project_name;
        String target_name;
        String output_directory;
        String worker_name;
        String socket_address;
        String indexed_data_store_path;

        bool include_repl;
        SocketMode socket_mode;
        SocketTransport socket_transport;
        bool include_debug_harness;
        bool include_thread_pool;
        bool include_threaded_worker;
        bool include_indexed_data_store;
        int indexed_data_store_bank_map_redundancy;
        int socket_port;
    };

}

#endif

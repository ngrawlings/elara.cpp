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

        ProjectOptions() :
            include_repl(true),
            socket_mode(SOCKET_DISABLED),
            include_thread_pool(false),
            include_threaded_worker(false),
            socket_port(4040) {
        }

        String project_name;
        String target_name;
        String output_directory;
        String worker_name;
        String socket_address;

        bool include_repl;
        SocketMode socket_mode;
        bool include_thread_pool;
        bool include_threaded_worker;
        int socket_port;
    };

}

#endif

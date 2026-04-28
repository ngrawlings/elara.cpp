#ifndef ElaraProjectBuilder_ProjectOptions_h
#define ElaraProjectBuilder_ProjectOptions_h

#include <libelaracore/memory/String.h>

namespace elara {

    class ProjectOptions {
    public:
        ProjectOptions() :
            include_repl(true),
            include_socket_server(false),
            include_thread_pool(false),
            include_threaded_worker(false) {
        }

        String project_name;
        String target_name;
        String output_directory;
        String worker_name;

        bool include_repl;
        bool include_socket_server;
        bool include_thread_pool;
        bool include_threaded_worker;
    };

}

#endif

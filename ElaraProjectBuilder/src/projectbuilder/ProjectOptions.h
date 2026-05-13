#ifndef ElaraProjectBuilder_ProjectOptions_h
#define ElaraProjectBuilder_ProjectOptions_h

#include <libelaracore/memory/String.h>

namespace elara {

    class ProjectOptions {
    public:
        enum ApplicationKind {
            APPLICATION_CONSOLE,
            APPLICATION_UI
        };

        enum UiClientLanguage {
            UI_CLIENT_CPP,
            UI_CLIENT_PYTHON
        };

        enum UiTemplate {
            UI_TEMPLATE_TABBED_CONTROL_PANEL,
            UI_TEMPLATE_RICH_EDITOR
        };

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
            application_kind(APPLICATION_CONSOLE),
            ui_client_language(UI_CLIENT_CPP),
            ui_template(UI_TEMPLATE_TABBED_CONTROL_PANEL),
            include_python_multi_cpu_template(false),
            include_repl(true),
            socket_mode(SOCKET_DISABLED),
            socket_transport(SOCKET_TRANSPORT_PLAIN),
            include_debug_harness(true),
            include_thread_pool(false),
            include_threaded_worker(false),
            include_epa_vm_host(false),
            include_epa_debug_rpc(false),
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

        ApplicationKind application_kind;
        UiClientLanguage ui_client_language;
        UiTemplate ui_template;
        bool include_python_multi_cpu_template;
        bool include_repl;
        SocketMode socket_mode;
        SocketTransport socket_transport;
        bool include_debug_harness;
        bool include_thread_pool;
        bool include_threaded_worker;
        bool include_epa_vm_host;
        bool include_epa_debug_rpc;
        bool include_indexed_data_store;
        int indexed_data_store_bank_map_redundancy;
        int socket_port;
    };

}

#endif

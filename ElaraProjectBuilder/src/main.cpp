#include "projectbuilder/ProjectBuilder.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <exception>

using namespace elara;

static String resolveExecutablePath(const char *argv0) {
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = 0;
        return String(buffer);
    }

    return String(argv0);
}

static bool matchesOption(const char *arg, const char *name) {
    size_t name_length = strlen(name);
    return !strcmp(arg, name) || (strncmp(arg, name, name_length) == 0 && arg[name_length] == '=');
}

static const char *optionValue(int argc, const char *argv[], int *index, const char *arg, const char *name) {
    size_t name_length = strlen(name);

    if (arg[name_length] == '=') {
        return arg + name_length + 1;
    }

    if (*index + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", name);
        return NULL;
    }

    *index += 1;
    return argv[*index];
}

static bool parseBoolValue(const char *value, bool *result) {
    if (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "yes") || !strcmp(value, "on")) {
        *result = true;
        return true;
    }
    if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "no") || !strcmp(value, "off")) {
        *result = false;
        return true;
    }
    return false;
}

static bool parseApplicationKindValue(const char *value, ProjectOptions::ApplicationKind *result) {
    if (!strcmp(value, "console")) {
        *result = ProjectOptions::APPLICATION_CONSOLE;
        return true;
    }
    if (!strcmp(value, "ui")) {
        *result = ProjectOptions::APPLICATION_UI;
        return true;
    }
    return false;
}

static bool parseUiClientLanguageValue(const char *value, ProjectOptions::UiClientLanguage *result) {
    if (!strcmp(value, "cpp") || !strcmp(value, "c++")) {
        *result = ProjectOptions::UI_CLIENT_CPP;
        return true;
    }
    if (!strcmp(value, "python")) {
        *result = ProjectOptions::UI_CLIENT_PYTHON;
        return true;
    }
    return false;
}

static bool parseUiTemplateValue(const char *value, ProjectOptions::UiTemplate *result) {
    if (!strcmp(value, "tabbed-control-panel")) {
        *result = ProjectOptions::UI_TEMPLATE_TABBED_CONTROL_PANEL;
        return true;
    }
    if (!strcmp(value, "rich-editor")) {
        *result = ProjectOptions::UI_TEMPLATE_RICH_EDITOR;
        return true;
    }
    return false;
}

static bool parseSocketModeValue(const char *value, ProjectOptions::SocketMode *result) {
    if (!strcmp(value, "none")) {
        *result = ProjectOptions::SOCKET_DISABLED;
        return true;
    }
    if (!strcmp(value, "server")) {
        *result = ProjectOptions::SOCKET_SERVER;
        return true;
    }
    if (!strcmp(value, "client")) {
        *result = ProjectOptions::SOCKET_CLIENT;
        return true;
    }
    return false;
}

static bool parseSocketTransportValue(const char *value, ProjectOptions::SocketTransport *result) {
    if (!strcmp(value, "plain")) {
        *result = ProjectOptions::SOCKET_TRANSPORT_PLAIN;
        return true;
    }
    if (!strcmp(value, "json-rpc")) {
        *result = ProjectOptions::SOCKET_TRANSPORT_JSON_RPC;
        return true;
    }
    return false;
}

static bool parsePortValue(const char *value, int *result) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);

    if (!end || *end || parsed <= 0 || parsed > 65535) {
        return false;
    }

    *result = (int)parsed;
    return true;
}

static bool parseNonNegativeIntValue(const char *value, int *result) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);

    if (!end || *end || parsed < 0 || parsed > 1024) {
        return false;
    }

    *result = (int)parsed;
    return true;
}

static void printUsage(const char *program_name) {
    printf("Usage: %s [options] [output-directory]\n", program_name);
    printf("\n");
    printf("Interactive mode is the default when no generation options are provided.\n");
    printf("Non-interactive mode is selected automatically when any generation option is set.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help                      Show this help text\n");
    printf("  --interactive               Force the prompt-driven flow\n");
    printf("  --non-interactive           Generate without prompts\n");
    printf("  --name <value>              Project class/name prefix\n");
    printf("  --target <value>            Executable name\n");
    printf("  --output <path>             Output directory\n");
    printf("  --app-kind <console|ui>\n");
    printf("  --ui-client-language <cpp|python>\n");
    printf("  --ui-template <tabbed-control-panel|rich-editor>\n");
    printf("  --multi-cpu-python <yes|no>  Enable the Python multi-core worker template for Python UI clients\n");
    printf("  --repl <yes|no>             Enable or disable the REPL\n");
    printf("  --debug-harness <yes|no>    Enable or disable the debug artifact scaffold\n");
    printf("  --thread-pool <yes|no>      Enable or disable the thread pool\n");
    printf("  --worker <yes|no>           Enable or disable the threaded worker template\n");
    printf("  --worker-name <value>       Worker class name\n");
    printf("  --indexed-data-store <yes|no>\n");
    printf("  --store-path <value>        IndexedDataStore file path inside the generated project\n");
    printf("  --store-bank-map-redundancy <value>\n");
    printf("  --socket-mode <none|server|client>\n");
    printf("  --socket-transport <plain|json-rpc>\n");
    printf("  --address <value>           Bind address for server or remote address for client\n");
    printf("  --port <value>              Socket port\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --output ./demo\n", program_name);
    printf("  %s --name SpiritResearch --target spirit-research --output ./spirit-research --socket-mode server --address 0.0.0.0 --port 4040 --thread-pool yes\n", program_name);
    printf("  %s --name SpiritProbe --target spirit-probe --output ./spirit-probe --socket-mode client --address 127.0.0.1 --port 4040 --repl yes\n", program_name);
}

int main(int argc, const char *argv[]) {
    try {
        ProjectBuilder builder;
        ProjectOptions options;
        bool interactive = true;
        bool saw_generation_option = false;
        bool saw_socket_address = false;
        bool saw_socket_port = false;
        bool saw_name = false;
        bool saw_target = false;
        bool saw_output = false;
        bool saw_worker_name = false;
        bool saw_worker_flag = false;
        bool saw_store_flag = false;
        bool saw_socket_transport = false;
        bool saw_ui_client_language = false;
        bool saw_ui_template = false;
        bool saw_multi_cpu_python = false;

        builder.setExecutablePath(resolveExecutablePath(argv[0]));
        options = builder.defaultOptions();

        for (int i = 1; i < argc; i++) {
            const char *arg = argv[i];

        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            printUsage(argv[0]);
            return 0;
        }

        if (!strcmp(arg, "--interactive")) {
            interactive = true;
            continue;
        }

        if (!strcmp(arg, "--non-interactive")) {
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--name")) {
            const char *value = optionValue(argc, argv, &i, arg, "--name");
            if (!value) {
                return 1;
            }
            options.project_name = String(value);
            saw_name = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--target")) {
            const char *value = optionValue(argc, argv, &i, arg, "--target");
            if (!value) {
                return 1;
            }
            options.target_name = String(value);
            saw_target = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--output")) {
            const char *value = optionValue(argc, argv, &i, arg, "--output");
            if (!value) {
                return 1;
            }
            options.output_directory = String(value);
            builder.setDefaultOutputDirectory(options.output_directory);
            saw_output = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--app-kind")) {
            const char *value = optionValue(argc, argv, &i, arg, "--app-kind");
            if (!value || !parseApplicationKindValue(value, &options.application_kind)) {
                fprintf(stderr, "Invalid value for --app-kind: %s\n", value ? value : "");
                return 1;
            }
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--ui-client-language")) {
            const char *value = optionValue(argc, argv, &i, arg, "--ui-client-language");
            if (!value || !parseUiClientLanguageValue(value, &options.ui_client_language)) {
                fprintf(stderr, "Invalid value for --ui-client-language: %s\n", value ? value : "");
                return 1;
            }
            saw_ui_client_language = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--ui-template")) {
            const char *value = optionValue(argc, argv, &i, arg, "--ui-template");
            if (!value || !parseUiTemplateValue(value, &options.ui_template)) {
                fprintf(stderr, "Invalid value for --ui-template: %s\n", value ? value : "");
                return 1;
            }
            saw_ui_template = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--multi-cpu-python")) {
            const char *value = optionValue(argc, argv, &i, arg, "--multi-cpu-python");
            if (!value || !parseBoolValue(value, &options.include_python_multi_cpu_template)) {
                fprintf(stderr, "Invalid value for --multi-cpu-python: %s\n", value ? value : "");
                return 1;
            }
            saw_multi_cpu_python = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--repl")) {
            const char *value = optionValue(argc, argv, &i, arg, "--repl");
            if (!value || !parseBoolValue(value, &options.include_repl)) {
                fprintf(stderr, "Invalid value for --repl: %s\n", value ? value : "");
                return 1;
            }
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--debug-harness")) {
            const char *value = optionValue(argc, argv, &i, arg, "--debug-harness");
            if (!value || !parseBoolValue(value, &options.include_debug_harness)) {
                fprintf(stderr, "Invalid value for --debug-harness: %s\n", value ? value : "");
                return 1;
            }
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--thread-pool")) {
            const char *value = optionValue(argc, argv, &i, arg, "--thread-pool");
            if (!value || !parseBoolValue(value, &options.include_thread_pool)) {
                fprintf(stderr, "Invalid value for --thread-pool: %s\n", value ? value : "");
                return 1;
            }
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--worker-name")) {
            const char *value = optionValue(argc, argv, &i, arg, "--worker-name");
            if (!value) {
                return 1;
            }
            options.worker_name = String(value);
            options.include_threaded_worker = true;
            saw_worker_name = true;
            saw_worker_flag = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--worker")) {
            const char *value = optionValue(argc, argv, &i, arg, "--worker");
            if (!value || !parseBoolValue(value, &options.include_threaded_worker)) {
                fprintf(stderr, "Invalid value for --worker: %s\n", value ? value : "");
                return 1;
            }
            saw_worker_flag = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--indexed-data-store")) {
            const char *value = optionValue(argc, argv, &i, arg, "--indexed-data-store");
            if (!value || !parseBoolValue(value, &options.include_indexed_data_store)) {
                fprintf(stderr, "Invalid value for --indexed-data-store: %s\n", value ? value : "");
                return 1;
            }
            saw_store_flag = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--store-path")) {
            const char *value = optionValue(argc, argv, &i, arg, "--store-path");
            if (!value) {
                return 1;
            }
            options.indexed_data_store_path = String(value);
            options.include_indexed_data_store = true;
            saw_store_flag = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--store-bank-map-redundancy")) {
            const char *value = optionValue(argc, argv, &i, arg, "--store-bank-map-redundancy");
            if (!value || !parseNonNegativeIntValue(value, &options.indexed_data_store_bank_map_redundancy)) {
                fprintf(stderr, "Invalid value for --store-bank-map-redundancy: %s\n", value ? value : "");
                return 1;
            }
            options.include_indexed_data_store = true;
            saw_store_flag = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--socket-mode")) {
            const char *value = optionValue(argc, argv, &i, arg, "--socket-mode");
            if (!value || !parseSocketModeValue(value, &options.socket_mode)) {
                fprintf(stderr, "Invalid value for --socket-mode: %s\n", value ? value : "");
                return 1;
            }
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--socket-transport")) {
            const char *value = optionValue(argc, argv, &i, arg, "--socket-transport");
            if (!value || !parseSocketTransportValue(value, &options.socket_transport)) {
                fprintf(stderr, "Invalid value for --socket-transport: %s\n", value ? value : "");
                return 1;
            }
            saw_socket_transport = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--address")) {
            const char *value = optionValue(argc, argv, &i, arg, "--address");
            if (!value) {
                return 1;
            }
            options.socket_address = String(value);
            saw_socket_address = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (matchesOption(arg, "--port")) {
            const char *value = optionValue(argc, argv, &i, arg, "--port");
            if (!value || !parsePortValue(value, &options.socket_port)) {
                fprintf(stderr, "Invalid value for --port: %s\n", value ? value : "");
                return 1;
            }
            saw_socket_port = true;
            interactive = false;
            saw_generation_option = true;
            continue;
        }

        if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            fprintf(stderr, "Run %s --help for usage.\n", argv[0]);
            return 1;
        }

        if (!saw_generation_option && interactive) {
            options.output_directory = String(arg);
            builder.setDefaultOutputDirectory(options.output_directory);
            continue;
        }

        fprintf(stderr, "Unexpected positional argument: %s\n", arg);
        return 1;
    }

        if (interactive && !saw_generation_option) {
            return builder.runInteractive() ? 0 : 1;
        }

        if (saw_multi_cpu_python &&
            (options.application_kind != ProjectOptions::APPLICATION_UI ||
             options.ui_client_language != ProjectOptions::UI_CLIENT_PYTHON)) {
            fprintf(stderr, "--multi-cpu-python is only valid for Python UI applications\n");
            return 1;
        }

        if (saw_name) {
            if (!saw_target) {
                options.target_name = options.project_name;
            }
            if (!saw_output) {
                options.output_directory = options.project_name;
            }
            if (!saw_worker_name && (!saw_worker_flag || options.include_threaded_worker)) {
                options.worker_name = options.project_name + "WorkerTask";
            }
        }

        if (options.socket_mode == ProjectOptions::SOCKET_DISABLED && (saw_socket_address || saw_socket_port)) {
            if (options.application_kind != ProjectOptions::APPLICATION_UI) {
                fprintf(stderr, "--address and --port require --socket-mode server or --socket-mode client\n");
                return 1;
            }
        }
        if (options.socket_mode == ProjectOptions::SOCKET_DISABLED && saw_socket_transport) {
            fprintf(stderr, "--socket-transport requires --socket-mode server or --socket-mode client\n");
            return 1;
        }
        if (options.application_kind != ProjectOptions::APPLICATION_UI && (saw_ui_client_language || saw_ui_template)) {
            fprintf(stderr, "--ui-client-language and --ui-template require --app-kind ui\n");
            return 1;
        }

        if (!saw_store_flag && options.include_indexed_data_store && !options.indexed_data_store_path.length()) {
            options.indexed_data_store_path = "data/store.dat";
        }

        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            if (!saw_socket_address) {
                options.socket_address = "127.0.0.1";
            }
            if (!saw_socket_port) {
                options.socket_port = 18777;
            }
        }

        return builder.generate(options) ? 0 : 1;
    } catch (const char *error) {
        fprintf(stderr, "Project builder error: %s\n", error);
        return 1;
    } catch (const std::exception &error) {
        fprintf(stderr, "Project builder error: %s\n", error.what());
        return 1;
    }
}

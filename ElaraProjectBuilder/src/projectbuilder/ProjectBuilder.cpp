#include "ProjectBuilder.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libelaraio/File.h>

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/StringList.h>

namespace elara {

    namespace {

        String boolTemplateValue(bool value) {
            return value ? String("1") : String("0");
        }

        String uiClientLanguageName(ProjectOptions::UiClientLanguage value) {
            if (value == ProjectOptions::UI_CLIENT_PYTHON) {
                return "python";
            }
            return "c++";
        }

        String uiTemplateName(ProjectOptions::UiTemplate value) {
            if (value == ProjectOptions::UI_TEMPLATE_RICH_EDITOR) {
                return "rich-editor";
            }
            return "tabbed-control-panel";
        }

        String applicationKindName(ProjectOptions::ApplicationKind value) {
            if (value == ProjectOptions::APPLICATION_UI) {
                return "ui";
            }
            return "console";
        }

        bool parseBoolText(String value, bool *result) {
            value = value.trim();
            if (value == String("1") || value == String("true") || value == String("yes") || value == String("on")) {
                if (result) {
                    *result = true;
                }
                return true;
            }
            if (value == String("0") || value == String("false") || value == String("no") || value == String("off")) {
                if (result) {
                    *result = false;
                }
                return true;
            }
            return false;
        }

        bool parseApplicationKindText(String value, ProjectOptions::ApplicationKind *result) {
            value = value.trim();
            if (value == String("ui")) {
                if (result) {
                    *result = ProjectOptions::APPLICATION_UI;
                }
                return true;
            }
            if (value == String("console")) {
                if (result) {
                    *result = ProjectOptions::APPLICATION_CONSOLE;
                }
                return true;
            }
            return false;
        }

        bool parseUiClientLanguageText(String value, ProjectOptions::UiClientLanguage *result) {
            value = value.trim();
            if (value == String("python")) {
                if (result) {
                    *result = ProjectOptions::UI_CLIENT_PYTHON;
                }
                return true;
            }
            if (value == String("cpp") || value == String("c++")) {
                if (result) {
                    *result = ProjectOptions::UI_CLIENT_CPP;
                }
                return true;
            }
            return false;
        }

        bool parseUiTemplateText(String value, ProjectOptions::UiTemplate *result) {
            value = value.trim();
            if (value == String("rich-editor")) {
                if (result) {
                    *result = ProjectOptions::UI_TEMPLATE_RICH_EDITOR;
                }
                return true;
            }
            if (value == String("tabbed-control-panel")) {
                if (result) {
                    *result = ProjectOptions::UI_TEMPLATE_TABBED_CONTROL_PANEL;
                }
                return true;
            }
            return false;
        }

        bool parseSocketModeText(String value, ProjectOptions::SocketMode *result) {
            value = value.trim();
            if (value == String("none")) {
                if (result) {
                    *result = ProjectOptions::SOCKET_DISABLED;
                }
                return true;
            }
            if (value == String("server")) {
                if (result) {
                    *result = ProjectOptions::SOCKET_SERVER;
                }
                return true;
            }
            if (value == String("client")) {
                if (result) {
                    *result = ProjectOptions::SOCKET_CLIENT;
                }
                return true;
            }
            return false;
        }

        bool parseSocketTransportText(String value, ProjectOptions::SocketTransport *result) {
            value = value.trim();
            if (value == String("plain")) {
                if (result) {
                    *result = ProjectOptions::SOCKET_TRANSPORT_PLAIN;
                }
                return true;
            }
            if (value == String("json-rpc")) {
                if (result) {
                    *result = ProjectOptions::SOCKET_TRANSPORT_JSON_RPC;
                }
                return true;
            }
            return false;
        }

        void appendTemplateAttr(String* attrs_text, const String& value) {
            if (!attrs_text) {
                return;
            }

            if (attrs_text->length()) {
                *attrs_text += ";";
            }

            *attrs_text += value;
        }

    }

    ProjectBuilder::ProjectBuilder() {
    }

    ProjectOptions ProjectBuilder::defaultOptions() {
        ProjectOptions options;

        options.project_name = "ElaraReplClient";
        options.target_name = sanitizeTargetName(options.project_name, "elara-app");
        options.output_directory = default_output_directory.length() ? default_output_directory : options.project_name;
        options.worker_name = projectClassPrefix(options.project_name) + "WorkerTask";
        options.socket_address = "0.0.0.0";
        options.indexed_data_store_path = "data/store.dat";

        return options;
    }

    void ProjectBuilder::setDefaultOutputDirectory(String output_directory) {
        if (output_directory.length()) {
            default_output_directory = output_directory;
        }
    }

    void ProjectBuilder::setExecutablePath(String executable_path) {
        this->executable_path = executable_path;
    }

    bool ProjectBuilder::runInteractive() {
        ProjectOptions options = promptOptions();
        return generate(options);
    }

    bool ProjectBuilder::generate(ProjectOptions options) {
        Array<PROJECT_FILE> files;

        normalizeOptions(options);

        if (!createProjectFiles(options, files)) {
            return false;
        }

        if (!writeProjectFiles(options.output_directory, files)) {
            return false;
        }

        printf("Project generated at %s\n", options.output_directory.operator char *());
        printf("Next steps:\n");
        printf("  cd %s\n", options.output_directory.operator char *());
        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            printf("  ./run-ui-head.sh\n");
            printf("  ./build.sh\n");
            printf("  ./run-client.sh\n");
        } else {
            printf("  ./build.sh\n");
            if (options.include_debug_harness) {
                printf("  ./debug.sh\n");
            }
        }
        printf("  sudo ./install.sh\n");
        return true;
    }

    String ProjectBuilder::loadTemplate(const String& template_name, const String& block_name, const StringList& attrs) {
        CodeTemplate tpl;
        tpl.setTemplateLoader(this);
        tpl.loadData(loadAsset(String("templates/src/%.tpl").arg(template_name)));
        return tpl.getCode(block_name, attrs);
    }

    ProjectOptions ProjectBuilder::promptOptions() {
        ProjectOptions options = defaultOptions();
        String project_name_default = options.project_name;
        String target_name_default;
        String output_directory_default;
        bool has_saved_config = false;

        printf("Elara Project Builder\n");
        printf("=====================\n");

        options.project_name = promptString("Project name", project_name_default);
        target_name_default = sanitizeTargetName(options.project_name, "elara-app");
        options.target_name = promptString("Executable name", target_name_default);

        output_directory_default = default_output_directory.length() ? default_output_directory : options.project_name;
        options.output_directory = promptString("Output directory", output_directory_default);
        has_saved_config = loadSavedProjectOptions(options.output_directory, &options);

        options.application_kind = promptApplicationKind();
        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            options.ui_client_language = promptUiClientLanguage();
            options.ui_template = promptUiTemplate();
            if (options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
                if (!has_saved_config) {
                    options.include_python_multi_cpu_template = promptPythonMultiCpuTemplate();
                }
            } else {
                options.include_python_multi_cpu_template = false;
            }
            options.socket_address = promptString("UI RPC server address", "127.0.0.1");
            options.socket_port = atoi(promptString("UI RPC server port", "18777").operator char *());
            options.include_repl = false;
            options.include_debug_harness = false;
            options.include_thread_pool = false;
            options.include_threaded_worker = false;
            options.include_indexed_data_store = false;
            options.socket_mode = ProjectOptions::SOCKET_DISABLED;
            options.socket_transport = ProjectOptions::SOCKET_TRANSPORT_JSON_RPC;
        } else {
            if (!has_saved_config) {
                options.include_repl = promptYesNo("Include REPL client", true);
            }
            options.socket_mode = promptSocketMode();
            if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
                options.socket_transport = promptSocketTransport();
            }
            if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
                options.socket_address = promptString("Socket bind address", "0.0.0.0");
                options.socket_port = atoi(promptString("Socket port", "4040").operator char *());
            } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
                options.socket_address = promptString("Socket remote address", "127.0.0.1");
                options.socket_port = atoi(promptString("Socket port", "4040").operator char *());
            }
            if (!has_saved_config) {
                options.include_thread_pool = promptYesNo("Include thread pool", false);
                options.include_threaded_worker = promptYesNo("Include threaded worker class", false);
                options.include_debug_harness = promptYesNo("Include debug harness and artifacts", true);
                options.include_indexed_data_store = promptYesNo("Include IndexedDataStore skeleton", false);
            }

            if (options.include_threaded_worker) {
                options.worker_name = promptString("Worker class name", projectClassPrefix(options.project_name) + "WorkerTask");
            }
            if (options.include_indexed_data_store) {
                options.indexed_data_store_path = promptString("IndexedDataStore path", "data/store.dat");
                options.indexed_data_store_bank_map_redundancy = atoi(promptString("IndexedDataStore bank map redundancy", "2").operator char *());
            }

            if (options.socket_mode != ProjectOptions::SOCKET_DISABLED && !options.include_thread_pool) {
                printf("Socket support requires a thread pool. Enabling it.\n");
                options.include_thread_pool = true;
            }

            if (options.include_threaded_worker && !options.include_thread_pool) {
                printf("Threaded workers require the thread pool. Enabling it.\n");
                options.include_thread_pool = true;
            }
        }

        return options;
    }

    ProjectOptions::ApplicationKind ProjectBuilder::promptApplicationKind() {
        char buffer[64];

        while (true) {
            printf("Application kind [c]onsole/[u]i: ");
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return ProjectOptions::APPLICATION_CONSOLE;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len || buffer[0] == 'c' || buffer[0] == 'C') {
                return ProjectOptions::APPLICATION_CONSOLE;
            }
            if (buffer[0] == 'u' || buffer[0] == 'U') {
                return ProjectOptions::APPLICATION_UI;
            }

            printf("Please answer c or u.\n");
        }
    }

    ProjectOptions::UiClientLanguage ProjectBuilder::promptUiClientLanguage() {
        char buffer[64];

        while (true) {
            printf("UI client language [c]++/[p]ython: ");
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return ProjectOptions::UI_CLIENT_CPP;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len || buffer[0] == 'c' || buffer[0] == 'C') {
                return ProjectOptions::UI_CLIENT_CPP;
            }
            if (buffer[0] == 'p' || buffer[0] == 'P') {
                return ProjectOptions::UI_CLIENT_PYTHON;
            }

            printf("Please answer c or p.\n");
        }
    }

    ProjectOptions::UiTemplate ProjectBuilder::promptUiTemplate() {
        char buffer[64];

        while (true) {
            printf("UI template [t]abbed-control-panel/[r]ich-editor: ");
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return ProjectOptions::UI_TEMPLATE_TABBED_CONTROL_PANEL;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len || buffer[0] == 't' || buffer[0] == 'T') {
                return ProjectOptions::UI_TEMPLATE_TABBED_CONTROL_PANEL;
            }
            if (buffer[0] == 'r' || buffer[0] == 'R') {
                return ProjectOptions::UI_TEMPLATE_RICH_EDITOR;
            }

            printf("Please answer t or r.\n");
        }
    }

    bool ProjectBuilder::promptPythonMultiCpuTemplate() {
        return promptYesNo("Generate the Python multi-core worker template", false);
    }

    ProjectOptions::SocketMode ProjectBuilder::promptSocketMode() {
        char buffer[64];

        while (true) {
            printf("Socket mode [n]one/[s]erver/[c]lient: ");
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return ProjectOptions::SOCKET_DISABLED;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len || buffer[0] == 'n' || buffer[0] == 'N') {
                return ProjectOptions::SOCKET_DISABLED;
            }
            if (buffer[0] == 's' || buffer[0] == 'S') {
                return ProjectOptions::SOCKET_SERVER;
            }
            if (buffer[0] == 'c' || buffer[0] == 'C') {
                return ProjectOptions::SOCKET_CLIENT;
            }

            printf("Please answer n, s, or c.\n");
        }
    }

    ProjectOptions::SocketTransport ProjectBuilder::promptSocketTransport() {
        char buffer[64];

        while (true) {
            printf("Socket transport [p]lain/[j]son-rpc: ");
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return ProjectOptions::SOCKET_TRANSPORT_PLAIN;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len || buffer[0] == 'p' || buffer[0] == 'P') {
                return ProjectOptions::SOCKET_TRANSPORT_PLAIN;
            }
            if (buffer[0] == 'j' || buffer[0] == 'J') {
                return ProjectOptions::SOCKET_TRANSPORT_JSON_RPC;
            }

            printf("Please answer p or j.\n");
        }
    }

    void ProjectBuilder::normalizeOptions(ProjectOptions &options) {
        options.project_name = sanitizeClassName(options.project_name, "ElaraGeneratedProject");
        options.target_name = sanitizeTargetName(options.target_name, "elara-generated-project");

        if (!options.output_directory.length()) {
            options.output_directory = options.target_name;
        }

        if (!options.worker_name.length()) {
            options.worker_name = projectClassPrefix(options.project_name) + "WorkerTask";
        }
        options.worker_name = sanitizeClassName(options.worker_name, "GeneratedWorkerTask");

        if (!options.socket_address.length()) {
            options.socket_address = options.socket_mode == ProjectOptions::SOCKET_CLIENT ? String("127.0.0.1") : String("0.0.0.0");
        }

        if (!options.indexed_data_store_path.length()) {
            options.indexed_data_store_path = "data/store.dat";
        }

        if (options.indexed_data_store_bank_map_redundancy < 0) {
            options.indexed_data_store_bank_map_redundancy = 0;
        }

        if (options.socket_port <= 0 || options.socket_port > 65535) {
            options.socket_port = options.application_kind == ProjectOptions::APPLICATION_UI ? 18777 : 4040;
        }

        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            options.include_repl = false;
            options.include_debug_harness = false;
            options.include_thread_pool = false;
            options.include_threaded_worker = false;
            options.include_indexed_data_store = false;
            options.socket_mode = ProjectOptions::SOCKET_DISABLED;
            options.socket_transport = ProjectOptions::SOCKET_TRANSPORT_JSON_RPC;
            if (options.ui_client_language != ProjectOptions::UI_CLIENT_PYTHON) {
                options.include_python_multi_cpu_template = false;
            }
            if (!options.socket_address.length()) {
                options.socket_address = "127.0.0.1";
            }
            return;
        }

        if (options.socket_mode != ProjectOptions::SOCKET_DISABLED || options.include_threaded_worker) {
            options.include_thread_pool = true;
        }
    }

    bool ProjectBuilder::promptYesNo(const char *prompt, bool default_value) {
        char buffer[64];

        while (true) {
            printf("%s [%c/%c]: ", prompt, default_value ? 'Y' : 'y', default_value ? 'n' : 'N');
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                return default_value;
            }

            size_t len = strlen(buffer);
            while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
                buffer[--len] = 0;
            }

            if (!len) {
                return default_value;
            }

            if (buffer[0] == 'y' || buffer[0] == 'Y') {
                return true;
            }
            if (buffer[0] == 'n' || buffer[0] == 'N') {
                return false;
            }

            printf("Please answer y or n.\n");
        }
    }

    String ProjectBuilder::promptString(const char *prompt, String default_value) {
        char buffer[512];

        printf("%s [%s]: ", prompt, default_value.operator char *());
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            return default_value;
        }

        size_t len = strlen(buffer);
        while (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[--len] = 0;
        }

        if (!len) {
            return default_value;
        }

        return String(buffer);
    }

    bool ProjectBuilder::createProjectFiles(const ProjectOptions &options, Array<PROJECT_FILE> &files) {
        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            addFile(files, "README.md", renderReadme(options));
            addFile(files, "ELARA_AGENT_API.md", loadAgentReference());
            addFile(files, ".elara-project-builder.config", renderProjectBuilderConfig(options));
            addFile(files, "build.sh", renderBuildScript(options));
            addFile(files, "install.sh", renderInstallScript(options));
            addFile(files, "run-ui-head.sh", renderRunUiHeadScript(options));
            addFile(files, "run-client.sh", renderRunUiClientScript(options));

            if (options.ui_client_language == ProjectOptions::UI_CLIENT_CPP) {
                addFile(files, "configure.ac", renderConfigureAc(options));
                addFile(files, "Makefile.in", renderMakefileIn(options));
                addFile(files, "src/main.cpp", renderUiCppMain(options));
            } else {
                addFile(files, "app.py", renderUiPythonApp(options));
                addFile(files, "elara_ui/__init__.py", renderUiPythonPackageInit(options));
                addFile(files, "elara_ui/builder.py", renderUiPythonPackageBuilder(options));
                addFile(files, "elara_ui/rpc.py", renderUiPythonPackageRpc(options));
                if (options.include_python_multi_cpu_template) {
                    addFile(files, "elara_ui/multi_cpu.py", renderUiPythonMultiCpuHelper(options));
                    addFile(files, "workers/worker_template.py", renderUiPythonWorkerTemplate(options));
                }
            }

            return true;
        }

        addFile(files, "configure.ac", renderConfigureAc(options));
        addFile(files, "Makefile.in", renderMakefileIn(options));
        addFile(files, "build.sh", renderBuildScript(options));
        if (options.include_debug_harness) {
            addFile(files, "debug.sh", renderDebugScript(options));
            addFile(files, "stress.sh", renderStressScript(options));
            addFile(files, "fuzz.sh", renderFuzzScript(options));
        }
        addFile(files, "install.sh", renderInstallScript(options));
        addFile(files, "README.md", renderReadme(options));
        addFile(files, "ELARA_AGENT_API.md", loadAgentReference());
        addFile(files, ".elara-project-builder.config", renderProjectBuilderConfig(options));
        addFile(files, "src/main.cpp", renderMainCpp(options));
        if (options.include_debug_harness) {
            String tests_name = projectClassPrefix(options.project_name) + "DebugTests";
            addFile(files, "tests/main.cpp", renderTestMainCpp(options));
            addFile(files, joinPath("tests", tests_name + ".h"), renderDebugTestsHeader(options));
            addFile(files, joinPath("tests", tests_name + ".cpp"), renderDebugTestsCpp(options));
        }

        if (options.include_threaded_worker) {
            addFile(files, joinPath("src", options.worker_name + ".h"), renderWorkerHeader(options));
            addFile(files, joinPath("src", options.worker_name + ".cpp"), renderWorkerCpp(options));
        }

        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            if (options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
                String server_name = projectClassPrefix(options.project_name) + "RpcServer";
                String service_name = projectClassPrefix(options.project_name) + "RpcService";
                addFile(files, joinPath("src", server_name + ".h"), renderJsonRPCServerHeader(options));
                addFile(files, joinPath("src", server_name + ".cpp"), renderJsonRPCServerCpp(options));
                addFile(files, joinPath("src", service_name + ".h"), renderJsonRPCServiceHeader(options));
                addFile(files, joinPath("src", service_name + ".cpp"), renderJsonRPCServiceCpp(options));
            } else {
                String server_name = projectClassPrefix(options.project_name) + "SocketServer";
                addFile(files, joinPath("src", server_name + ".h"), renderSocketServerHeader(options));
                addFile(files, joinPath("src", server_name + ".cpp"), renderSocketServerCpp(options));
            }
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            if (options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
                String client_name = projectClassPrefix(options.project_name) + "RpcClient";
                addFile(files, joinPath("src", client_name + ".h"), renderJsonRPCClientHeader(options));
                addFile(files, joinPath("src", client_name + ".cpp"), renderJsonRPCClientCpp(options));
            } else {
                String client_name = projectClassPrefix(options.project_name) + "SocketClient";
                addFile(files, joinPath("src", client_name + ".h"), renderSocketClientHeader(options));
                addFile(files, joinPath("src", client_name + ".cpp"), renderSocketClientCpp(options));
            }
        }

        return true;
    }

    void ProjectBuilder::addFile(Array<PROJECT_FILE> &files, String path, String contents) {
        PROJECT_FILE file;
        file.path = path;
        file.contents = contents;
        files.push(file);
    }

    String ProjectBuilder::renderConfigureAc(const ProjectOptions &options) {
        String contents;
        contents += "AC_INIT([";
        contents += options.project_name;
        contents += "], [0.1])\n";
        contents += "AC_PROG_CXX\n";
        contents += "AC_CONFIG_FILES([Makefile])\n";
        contents += "AC_OUTPUT\n";
        return contents;
    }

    String ProjectBuilder::renderMakefileIn(const ProjectOptions &options) {
        if (options.application_kind == ProjectOptions::APPLICATION_UI &&
            options.ui_client_language == ProjectOptions::UI_CLIENT_CPP) {
            String contents;
            contents += "NAME=";
            contents += options.target_name;
            contents += "\n";
            contents += "CC=@CXX@\n";
            contents += "DEFS =\n";
            contents += "ELARA_ROOT?=$(abspath ../build)\n";
            contents += "ELARA_INCLUDE_DIR?=$(ELARA_ROOT)/include\n";
            contents += "ELARA_LIB_DIR?=$(ELARA_ROOT)/lib\n";
            contents += "PREFIX?=$(abspath ./dist)\n";
            contents += "BIN_DIR?=$(PREFIX)/bin\n";
            contents += "SHARE_DIR?=$(PREFIX)/share/";
            contents += options.target_name;
            contents += "\n";
            contents += "INSTALL_MANIFEST?=$(SHARE_DIR)/install-manifest.txt\n";
            contents += "BUILDPATH=./build\n";
            contents += "COMMON_CFLAGS=-std=gnu++11\n";
            contents += "DEBUG_CFLAGS=$(COMMON_CFLAGS) -O0 -g3\n";
            contents += "RELEASE_CFLAGS=$(COMMON_CFLAGS) -O3 -DNDEBUG\n";
            contents += "BUILD_PROFILE?=release\n";
            contents += "ifeq ($(BUILD_PROFILE),debug)\n";
            contents += "PROFILE_CFLAGS=$(DEBUG_CFLAGS)\n";
            contents += "else\n";
            contents += "PROFILE_CFLAGS=$(RELEASE_CFLAGS)\n";
            contents += "endif\n";
            contents += "STD_CFLAGS=-c $(PROFILE_CFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) $(DEFS)\n";
            contents += "STD_LDFLAGS=-L$(ELARA_LIB_DIR) -lelarauirpc -lelarasockets -lelaraformat -lelaraio -lelaradebug -lelaraevent -lelarathreads -lelaracore -pthread\n";
            contents += "TARGET=";
            contents += options.target_name;
            contents += "\n";
            contents += "SOURCES=$(shell find ./src -type f -name '*.cpp' -print)\n";
            contents += "OBJECTS=$(patsubst ./src/%.cpp,build/src/%.o,$(SOURCES))\n\n";
            contents += ".PHONY: all install clean remove cleanconf\n\n";
            contents += "all: $(TARGET)\n\n";
            contents += "$(TARGET): $(OBJECTS)\n";
            contents += "\t$(CC) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)\n\n";
            contents += "build/src/%.o: ./src/%.cpp\n";
            contents += "\t@mkdir -p $(dir $@)\n";
            contents += "\t$(CC) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@\n\n";
            contents += "install:\n";
            contents += "\t@if [ ! -x \"$(BUILDPATH)/$(TARGET)\" ]; then \\\n";
            contents += "\t\techo \"Missing built binary $(BUILDPATH)/$(TARGET). Run ./build.sh first.\"; \\\n";
            contents += "\t\texit 1; \\\n";
            contents += "\tfi\n";
            contents += "\tmkdir -p $(BIN_DIR)\n";
            contents += "\tcp $(BUILDPATH)/$(TARGET) $(BIN_DIR)/$(TARGET)\n";
            contents += "\tmkdir -p $(SHARE_DIR)\n";
            contents += "\tprintf '%s\\n' \"$(BIN_DIR)/$(TARGET)\" > $(INSTALL_MANIFEST)\n\n";
            contents += "clean:\n";
            contents += "\trm -rf $(BUILDPATH)\n\n";
            contents += "remove:\n";
            contents += "\t@if [ -f \"$(INSTALL_MANIFEST)\" ]; then \\\n";
            contents += "\t\twhile IFS= read -r installed_path; do \\\n";
            contents += "\t\t\trm -f \"$$installed_path\"; \\\n";
            contents += "\t\tdone < \"$(INSTALL_MANIFEST)\"; \\\n";
            contents += "\t\trm -f \"$(INSTALL_MANIFEST)\"; \\\n";
            contents += "\telse \\\n";
            contents += "\t\trm -f $(BIN_DIR)/$(TARGET); \\\n";
            contents += "\tfi\n";
            contents += "\t@rmdir $(SHARE_DIR) 2>/dev/null || true\n\n";
            contents += "cleanconf:\n";
            contents += "\trm -f config.log\n";
            contents += "\trm -f config.status\n";
            contents += "\trm -f Makefile\n";
            contents += "\trm -f configure\n";
            return contents;
        }

        String contents;

        contents += "NAME=";
        contents += options.target_name;
        contents += "\n";
        contents += "CC=@CXX@\n";
        contents += "DEFS =\n";
        contents += "ELARA_ROOT?=$(abspath ../build)\n";
        contents += "ELARA_INCLUDE_DIR?=$(ELARA_ROOT)/include\n";
        contents += "ELARA_LIB_DIR?=$(ELARA_ROOT)/lib\n";
        contents += "ELARA_BIN_DIR?=$(ELARA_ROOT)/bin\n";
        contents += "ELARA_CPP_LINT_LOCAL?=$(ELARA_BIN_DIR)/elara.cpp-lint\n";
        contents += "ELARA_CPP_LINT_SYSTEM?=/usr/local/bin/elara.cpp-lint\n";
        contents += "ELARA_CPP_LINT?=$(shell if [ -x \"$(ELARA_CPP_LINT_LOCAL)\" ]; then printf '%s' \"$(ELARA_CPP_LINT_LOCAL)\"; else printf '%s' \"$(ELARA_CPP_LINT_SYSTEM)\"; fi)\n";
        contents += "PREFIX?=$(abspath ./dist)\n";
        contents += "BIN_DIR?=$(PREFIX)/bin\n";
        contents += "SHARE_DIR?=$(PREFIX)/share/";
        contents += options.target_name;
        contents += "\n";
        contents += "INSTALL_MANIFEST?=$(SHARE_DIR)/install-manifest.txt\n";
        contents += "BUILDPATH=./build\n";
        contents += "BUILD_STATE_FILE=$(BUILDPATH)/.build-state\n";
        contents += "BUILD_STATE_TMP=.build-state.tmp\n";
        contents += "BUILD_PROFILE?=release\n";
        contents += "COMMON_CFLAGS=-std=gnu++11\n";
        contents += "DEBUG_CFLAGS=$(COMMON_CFLAGS) -O0 -g3\n";
        contents += "RELEASE_CFLAGS=$(COMMON_CFLAGS) -O3 -DNDEBUG\n";
        contents += "ASAN_CFLAGS=$(COMMON_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined\n";
        contents += "DEBUG_LDFLAGS=\n";
        contents += "RELEASE_LDFLAGS=\n";
        contents += "ASAN_LDFLAGS=-fsanitize=address,undefined\n";
        contents += "ifeq ($(BUILD_PROFILE),debug)\n";
        contents += "PROFILE_CFLAGS=$(DEBUG_CFLAGS)\n";
        contents += "PROFILE_LDFLAGS=$(DEBUG_LDFLAGS)\n";
        contents += "else ifeq ($(BUILD_PROFILE),release)\n";
        contents += "PROFILE_CFLAGS=$(RELEASE_CFLAGS)\n";
        contents += "PROFILE_LDFLAGS=$(RELEASE_LDFLAGS)\n";
        contents += "else ifeq ($(BUILD_PROFILE),asan)\n";
        contents += "PROFILE_CFLAGS=$(ASAN_CFLAGS)\n";
        contents += "PROFILE_LDFLAGS=$(ASAN_LDFLAGS)\n";
        contents += "else\n";
        contents += "$(error Unsupported BUILD_PROFILE '$(BUILD_PROFILE)'. Use debug, release, or asan)\n";
        contents += "endif\n";
        contents += "STD_CFLAGS=-c $(PROFILE_CFLAGS) -I$(shell pwd) -I$(ELARA_INCLUDE_DIR) $(DEFS)\n";
        contents += "STD_LDFLAGS=$(PROFILE_LDFLAGS) -L$(ELARA_LIB_DIR) ";
        contents += buildElaraLibFlags(options);
        String system_flags = buildSystemLibFlags(options);
        if (system_flags.length()) {
            contents += " ";
            contents += system_flags;
        }
        contents += "\n";
        contents += "SOURCES=$(shell find ./src -type f -name '*.cpp' -print)\n";
        contents += "OBJECTS=$(patsubst ./src/%.cpp,build/src/%.o,$(SOURCES))\n";
        if (options.include_debug_harness) {
            contents += "TEST_SOURCES=$(shell find ./tests -type f -name '*.cpp' -print)\n";
            contents += "TEST_OBJECTS=$(patsubst ./tests/%.cpp,build/tests/%.o,$(TEST_SOURCES))\n";
            contents += "TEST_TARGET=$(TARGET)-debug\n";
            contents += "ARTIFACT_ROOT?=./artifacts\n";
            contents += "TEST_DURATION_SECONDS?=30\n";
            contents += "TEST_RUNNER=$(BUILDPATH)/$(TEST_TARGET)\n";
            contents += "TEST_STD_LDFLAGS=$(PROFILE_LDFLAGS) -L$(ELARA_LIB_DIR) -lelaradebug -lelaraio ";
            contents += buildElaraLibFlags(options);
            if (system_flags.length()) {
                contents += " ";
                contents += system_flags;
            }
            contents += "\n";
            contents += "LINT_PATHS=./src ./tests\n";
        } else {
            contents += "LINT_PATHS=./src\n";
        }
        contents += "LINT_STAMP=$(BUILDPATH)/.lint-ok\n";
        contents += "TARGET=";
        contents += options.target_name;
        contents += "\n\n";
        contents += ".PHONY: all lint install clean remove cleanconf prepare-build";
        if (options.include_debug_harness) {
            contents += " tests debug stress fuzz";
        }
        contents += "\n\n";
        contents += "all: $(TARGET)\n\n";
        contents += "prepare-build:\n";
        contents += "\t@mkdir -p $(BUILDPATH)\n";
        contents += "\t@printf '%s\\n' \"BUILD_PROFILE=$(BUILD_PROFILE)\" > $(BUILD_STATE_TMP)\n";
        contents += "\t@printf '%s\\n' \"PROFILE_CFLAGS=$(PROFILE_CFLAGS)\" >> $(BUILD_STATE_TMP)\n";
        contents += "\t@printf '%s\\n' \"PROFILE_LDFLAGS=$(PROFILE_LDFLAGS)\" >> $(BUILD_STATE_TMP)\n";
        contents += "\t@printf '%s\\n' \"STD_LDFLAGS=$(STD_LDFLAGS)\" >> $(BUILD_STATE_TMP)\n";
        contents += "\t@if [ -d \"$(BUILDPATH)\" ] && [ ! -f \"$(BUILD_STATE_FILE)\" ] && [ -n \"$$(find \"$(BUILDPATH)\" -mindepth 1 -print -quit)\" ]; then \\\n";
        contents += "\t\techo \"Cleaning due to missing build state\"; \\\n";
        contents += "\t\trm -rf \"$(BUILDPATH)\"; \\\n";
        contents += "\t\tmkdir -p \"$(BUILDPATH)\"; \\\n";
        contents += "\tfi\n";
        contents += "\t@if [ -f \"$(BUILD_STATE_FILE)\" ] && ! cmp -s \"$(BUILD_STATE_FILE)\" \"$(BUILD_STATE_TMP)\"; then \\\n";
        contents += "\t\techo \"Cleaning due to build configuration change\"; \\\n";
        contents += "\t\trm -rf \"$(BUILDPATH)\"; \\\n";
        contents += "\t\tmkdir -p \"$(BUILDPATH)\"; \\\n";
        contents += "\tfi\n";
        contents += "\t@mkdir -p $(BUILDPATH)\n";
        contents += "\t@mv $(BUILD_STATE_TMP) $(BUILD_STATE_FILE)\n\n";
        contents += "$(TARGET): $(OBJECTS)\n";
        contents += "\t$(CC) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)\n\n";
        if (options.include_debug_harness) {
            contents += "$(TEST_TARGET): $(TEST_OBJECTS)\n";
            contents += "\t$(CC) $(TEST_OBJECTS) $(TEST_STD_LDFLAGS) $(LDFLAGS) -o $(TEST_RUNNER)\n\n";
        }
        contents += "$(LINT_STAMP): $(SOURCES) Makefile";
        if (options.include_debug_harness) {
            contents += " $(TEST_SOURCES)";
        }
        contents += "\n";
        contents += "\t@mkdir -p $(BUILDPATH)\n";
        contents += "\t@if [ ! -x \"$(ELARA_CPP_LINT)\" ]; then \\\n";
        contents += "\t\techo \"Missing elara.cpp-lint. Checked $(ELARA_CPP_LINT_LOCAL) and $(ELARA_CPP_LINT_SYSTEM). Build ElaraCppLint locally, install it to /usr/local, or set ELARA_CPP_LINT=/path/to/elara.cpp-lint.\"; \\\n";
        contents += "\t\texit 1; \\\n";
        contents += "\tfi\n";
        contents += "\t$(ELARA_CPP_LINT) $(LINT_PATHS)\n";
        contents += "\t@touch $(LINT_STAMP)\n\n";
        contents += "build/src/%.o: ./src/%.cpp prepare-build $(LINT_STAMP)\n";
        contents += "\t@mkdir -p $(dir $@)\n";
        contents += "\t$(CC) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@\n\n";
        if (options.include_debug_harness) {
            contents += "build/tests/%.o: ./tests/%.cpp prepare-build $(LINT_STAMP)\n";
            contents += "\t@mkdir -p $(dir $@)\n";
            contents += "\t$(CC) $(STD_CFLAGS) $(CFLAGS) ./tests/$*.cpp -o $@\n\n";
        }
        contents += "lint: $(LINT_STAMP)\n\n";
        if (options.include_debug_harness) {
            contents += "tests: $(TEST_TARGET)\n\n";
            contents += "debug: $(TEST_TARGET)\n";
            contents += "\tELARA_DEBUG_MODE=validator ELARA_DEBUG_ARTIFACT_ROOT=\"$(ARTIFACT_ROOT)\" $(TEST_RUNNER)\n\n";
            contents += "stress: $(TEST_TARGET)\n";
            contents += "\tELARA_DEBUG_MODE=stress ELARA_DEBUG_DURATION_SECONDS=\"$(TEST_DURATION_SECONDS)\" ELARA_DEBUG_ARTIFACT_ROOT=\"$(ARTIFACT_ROOT)\" $(TEST_RUNNER)\n\n";
            contents += "fuzz: $(TEST_TARGET)\n";
            contents += "\tASAN_OPTIONS=\"detect_leaks=0\" LSAN_OPTIONS=\"detect_leaks=0\" ELARA_DEBUG_MODE=fuzz ELARA_DEBUG_DURATION_SECONDS=\"$(TEST_DURATION_SECONDS)\" ELARA_DEBUG_ARTIFACT_ROOT=\"$(ARTIFACT_ROOT)\" $(TEST_RUNNER)\n\n";
        }
        contents += "install:\n";
        contents += "\t@if [ ! -x \"$(BUILDPATH)/$(TARGET)\" ]; then \\\n";
        contents += "\t\techo \"Missing built binary $(BUILDPATH)/$(TARGET). Run ./build.sh first.\"; \\\n";
        contents += "\t\texit 1; \\\n";
        contents += "\tfi\n";
        contents += "\tmkdir -p $(BIN_DIR)\n";
        contents += "\tcp $(BUILDPATH)/$(TARGET) $(BIN_DIR)/$(TARGET)\n";
        contents += "\tmkdir -p $(SHARE_DIR)\n";
        contents += "\tprintf '%s\\n' \"$(BIN_DIR)/$(TARGET)\" > $(INSTALL_MANIFEST)\n\n";
        contents += "clean:\n";
        contents += "\trm -rf $(BUILDPATH)\n\n";
        contents += "remove:\n";
        contents += "\t@if [ -f \"$(INSTALL_MANIFEST)\" ]; then \\\n";
        contents += "\t\twhile IFS= read -r installed_path; do \\\n";
        contents += "\t\t\trm -f \"$$installed_path\"; \\\n";
        contents += "\t\tdone < \"$(INSTALL_MANIFEST)\"; \\\n";
        contents += "\t\trm -f \"$(INSTALL_MANIFEST)\"; \\\n";
        contents += "\telse \\\n";
        contents += "\t\trm -f $(BIN_DIR)/$(TARGET); \\\n";
        contents += "\tfi\n";
        contents += "\t@rmdir $(SHARE_DIR) 2>/dev/null || true\n\n";
        contents += "cleanconf:\n";
        contents += "\trm -f config.log\n";
        contents += "\trm -f config.status\n";
        contents += "\trm -f Makefile\n";
        contents += "\trm -f configure\n";

        return contents;
    }

    String ProjectBuilder::renderBuildScript(const ProjectOptions &options) {
        if (options.application_kind == ProjectOptions::APPLICATION_UI &&
            options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
            String contents;
            contents += "#!/usr/bin/env bash\n\n";
            contents += "set -euo pipefail\n\n";
            contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
            contents += "cd \"$ROOT_DIR\"\n\n";
            contents += "mkdir -p ./build\n";
            contents += "python3 -m py_compile app.py elara_ui/__init__.py elara_ui/builder.py elara_ui/rpc.py";
            if (options.include_python_multi_cpu_template) {
                contents += " elara_ui/multi_cpu.py workers/worker_template.py";
            }
            contents += "\n";
            contents += "printf '%s\\n' \"python build ok\" > ./build/build-stamp.txt\n";
            return contents;
        }

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";
        contents += "BUILD_PROFILE=\"${BUILD_PROFILE:-release}\"\n\n";
        contents += "autoreconf -fi\n";
        contents += "./configure\n";
        contents += "make BUILD_PROFILE=\"${BUILD_PROFILE}\" \"$@\"\n";
        return contents;
    }

    String ProjectBuilder::renderDebugScript(const ProjectOptions &options) {
        (void)options;

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";
        contents += "BUILD_PROFILE=\"${BUILD_PROFILE:-debug}\"\n";
        contents += "ARTIFACT_ROOT=\"${ARTIFACT_ROOT:-./artifacts}\"\n\n";
        contents += "autoreconf -fi\n";
        contents += "./configure\n";
        contents += "make debug BUILD_PROFILE=\"${BUILD_PROFILE}\" ARTIFACT_ROOT=\"${ARTIFACT_ROOT}\" \"$@\"\n";
        return contents;
    }

    String ProjectBuilder::renderStressScript(const ProjectOptions &options) {
        (void)options;

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";
        contents += "BUILD_PROFILE=\"${BUILD_PROFILE:-debug}\"\n";
        contents += "ARTIFACT_ROOT=\"${ARTIFACT_ROOT:-./artifacts}\"\n";
        contents += "TEST_DURATION_SECONDS=\"${TEST_DURATION_SECONDS:-${1:-30}}\"\n\n";
        contents += "autoreconf -fi\n";
        contents += "./configure\n";
        contents += "make stress BUILD_PROFILE=\"${BUILD_PROFILE}\" ARTIFACT_ROOT=\"${ARTIFACT_ROOT}\" TEST_DURATION_SECONDS=\"${TEST_DURATION_SECONDS}\"\n";
        return contents;
    }

    String ProjectBuilder::renderFuzzScript(const ProjectOptions &options) {
        (void)options;

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";
        contents += "BUILD_PROFILE=\"${BUILD_PROFILE:-asan}\"\n";
        contents += "ARTIFACT_ROOT=\"${ARTIFACT_ROOT:-./artifacts}\"\n";
        contents += "TEST_DURATION_SECONDS=\"${TEST_DURATION_SECONDS:-${1:-30}}\"\n\n";
        contents += "autoreconf -fi\n";
        contents += "./configure\n";
        contents += "ASAN_OPTIONS=\"detect_leaks=0\" LSAN_OPTIONS=\"detect_leaks=0\" make fuzz BUILD_PROFILE=\"${BUILD_PROFILE}\" ARTIFACT_ROOT=\"${ARTIFACT_ROOT}\" TEST_DURATION_SECONDS=\"${TEST_DURATION_SECONDS}\"\n";
        return contents;
    }

    String ProjectBuilder::renderInstallScript(const ProjectOptions &options) {
        if (options.application_kind == ProjectOptions::APPLICATION_UI &&
            options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
            String contents;
            contents += "#!/usr/bin/env bash\n\n";
            contents += "set -euo pipefail\n\n";
            contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
            contents += "cd \"$ROOT_DIR\"\n\n";
            contents += "INSTALL_PREFIX=\"${INSTALL_PREFIX:-/usr/local}\"\n";
            contents += "TARGET_NAME=\"";
            contents += options.target_name;
            contents += "\"\n";
            contents += "APP_DIR=\"${INSTALL_PREFIX}/share/${TARGET_NAME}\"\n";
            contents += "BIN_PATH=\"${INSTALL_PREFIX}/bin/${TARGET_NAME}\"\n";
            contents += "MANIFEST_PATH=\"${APP_DIR}/install-manifest.txt\"\n\n";
            contents += "if [[ \"${1:-}\" == \"--remove\" ]]; then\n";
            contents += "  if [[ -f \"${MANIFEST_PATH}\" ]]; then\n";
            contents += "    while IFS= read -r installed_path; do rm -f \"${installed_path}\"; done < \"${MANIFEST_PATH}\"\n";
            contents += "    rm -f \"${MANIFEST_PATH}\"\n";
            contents += "  fi\n";
            contents += "  rm -rf \"${APP_DIR}\"\n";
            contents += "  rm -f \"${BIN_PATH}\"\n";
            contents += "  exit 0\n";
            contents += "fi\n\n";
            contents += "mkdir -p \"${APP_DIR}\" \"${INSTALL_PREFIX}/bin\" \"${APP_DIR}/elara_ui\"\n";
            contents += "cp app.py \"${APP_DIR}/app.py\"\n";
            contents += "cp elara_ui/__init__.py \"${APP_DIR}/elara_ui/__init__.py\"\n";
            contents += "cp elara_ui/builder.py \"${APP_DIR}/elara_ui/builder.py\"\n";
            contents += "cp elara_ui/rpc.py \"${APP_DIR}/elara_ui/rpc.py\"\n";
            if (options.include_python_multi_cpu_template) {
                contents += "mkdir -p \"${APP_DIR}/workers\"\n";
                contents += "cp elara_ui/multi_cpu.py \"${APP_DIR}/elara_ui/multi_cpu.py\"\n";
                contents += "cp workers/worker_template.py \"${APP_DIR}/workers/worker_template.py\"\n";
            }
            contents += "cat > \"${BIN_PATH}\" <<EOF\n";
            contents += "#!/usr/bin/env bash\n";
            contents += "exec python3 \"${APP_DIR}/app.py\" \"\\$@\"\n";
            contents += "EOF\n";
            contents += "chmod +x \"${BIN_PATH}\"\n";
            contents += "printf '%s\\n' \"${BIN_PATH}\" > \"${MANIFEST_PATH}\"\n";
            contents += "printf '%s\\n' \"${APP_DIR}/app.py\" >> \"${MANIFEST_PATH}\"\n";
            contents += "printf '%s\\n' \"${APP_DIR}/elara_ui/__init__.py\" >> \"${MANIFEST_PATH}\"\n";
            contents += "printf '%s\\n' \"${APP_DIR}/elara_ui/builder.py\" >> \"${MANIFEST_PATH}\"\n";
            contents += "printf '%s\\n' \"${APP_DIR}/elara_ui/rpc.py\" >> \"${MANIFEST_PATH}\"\n";
            if (options.include_python_multi_cpu_template) {
                contents += "printf '%s\\n' \"${APP_DIR}/elara_ui/multi_cpu.py\" >> \"${MANIFEST_PATH}\"\n";
                contents += "printf '%s\\n' \"${APP_DIR}/workers/worker_template.py\" >> \"${MANIFEST_PATH}\"\n";
            }
            return contents;
        }

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";
        contents += "BUILD_PROFILE=\"${BUILD_PROFILE:-release}\"\n";
        contents += "INSTALL_PREFIX=\"${INSTALL_PREFIX:-/usr/local}\"\n";
        contents += "TARGET_NAME=\"";
        contents += options.target_name;
        contents += "\"\n";
        contents += "BUILD_TARGET=\"$ROOT_DIR/build/${TARGET_NAME}\"\n";
        contents += "MANIFEST_DIR=\"${INSTALL_PREFIX}/share/${TARGET_NAME}\"\n";
        contents += "MANIFEST_PATH=\"${MANIFEST_DIR}/install-manifest.txt\"\n\n";
        contents += "if [[ \"${1:-}\" == \"--remove\" ]]; then\n";
        contents += "  echo \"Removing from ${INSTALL_PREFIX}...\"\n";
        contents += "  if [[ -f \"${MANIFEST_PATH}\" ]]; then\n";
        contents += "    while IFS= read -r installed_path; do\n";
        contents += "      rm -f \"${installed_path}\"\n";
        contents += "    done < \"${MANIFEST_PATH}\"\n";
        contents += "    rm -f \"${MANIFEST_PATH}\"\n";
        contents += "    rmdir \"${MANIFEST_DIR}\" 2>/dev/null || true\n";
        contents += "  else\n";
        contents += "    make remove BUILD_PROFILE=\"${BUILD_PROFILE}\" PREFIX=\"${INSTALL_PREFIX}\"\n";
        contents += "  fi\n";
        contents += "  exit 0\n";
        contents += "fi\n\n";
        contents += "if [[ ! -x \"$BUILD_TARGET\" ]]; then\n";
        contents += "  echo \"Missing built binary $BUILD_TARGET. Run ./build.sh first, then rerun this installer.\"\n";
        contents += "  exit 1\n";
        contents += "fi\n\n";
        contents += "echo \"Installing into ${INSTALL_PREFIX}...\"\n";
        contents += "make install BUILD_PROFILE=\"${BUILD_PROFILE}\" PREFIX=\"${INSTALL_PREFIX}\"\n";
        contents += "mkdir -p \"${MANIFEST_DIR}\"\n";
        contents += "printf '%s\\n' \"${INSTALL_PREFIX}/bin/${TARGET_NAME}\" > \"${MANIFEST_PATH}\"\n";
        return contents;
    }

    String ProjectBuilder::renderRunUiHeadScript(const ProjectOptions &options) {
        (void)options;

        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "LOCAL_FRAMEWORK_SERVER=\"$(cd \"${ROOT_DIR}/../elara.cpp\" 2>/dev/null && pwd)/build/bin/elaraui-server\"\n";
        contents += "DEFAULT_SERVER=\"/usr/local/bin/elaraui-server\"\n";
        contents += "if [[ -x \"${LOCAL_FRAMEWORK_SERVER}\" ]]; then\n";
        contents += "  RESOLVED_SERVER=\"${LOCAL_FRAMEWORK_SERVER}\"\n";
        contents += "else\n";
        contents += "  RESOLVED_SERVER=\"${DEFAULT_SERVER}\"\n";
        contents += "fi\n";
        contents += "ELARA_UI_SERVER=\"${ELARA_UI_SERVER:-${RESOLVED_SERVER}}\"\n\n";
        contents += "if [[ ! -x \"${ELARA_UI_SERVER}\" ]]; then\n";
        contents += "  echo \"Missing Elara UI server at ${ELARA_UI_SERVER}. Install the framework server or set ELARA_UI_SERVER to the correct path.\"\n";
        contents += "  exit 1\n";
        contents += "fi\n\n";
        contents += "exec \"${ELARA_UI_SERVER}\" \"$@\"\n";
        return contents;
    }

    String ProjectBuilder::renderRunUiClientScript(const ProjectOptions &options) {
        String contents;
        contents += "#!/usr/bin/env bash\n\n";
        contents += "set -euo pipefail\n\n";
        contents += "ROOT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n";
        contents += "cd \"$ROOT_DIR\"\n\n";

        if (options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
            contents += "exec python3 ./app.py \"$@\"\n";
        } else {
            contents += "if [[ ! -x \"./build/";
            contents += options.target_name;
            contents += "\" ]]; then\n";
            contents += "  ./build.sh\n";
            contents += "fi\n";
            contents += "exec ./build/";
            contents += options.target_name;
            contents += " \"$@\"\n";
        }

        return contents;
    }

    String ProjectBuilder::renderReadme(const ProjectOptions &options) {
        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            String contents;

            contents += "# ";
            contents += options.project_name;
            contents += "\n\n";
            contents += "Generated by `elara-project-builder` as an Elara UI RPC client application.\n\n";
            contents += "Selected features:\n";
            contents += "- Application kind: ui\n";
            contents += "- UI client language: ";
            contents += uiClientLanguageName(options.ui_client_language);
            contents += "\n";
            contents += "- UI template: ";
            contents += uiTemplateName(options.ui_template);
            contents += "\n";
            if (options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
                contents += "- Python multi-core worker template: ";
                contents += options.include_python_multi_cpu_template ? "yes\n" : "no\n";
            }
            contents += "- UI RPC server address: ";
            contents += options.socket_address;
            contents += "\n";
            contents += "- UI RPC server port: ";
            contents += String(options.socket_port);
            contents += "\n\n";
            contents += "Quick start:\n";
            contents += "1. start the UI head with `./run-ui-head.sh`\n";
            contents += "2. build the client with `./build.sh`\n";
            contents += "3. launch the client with `./run-client.sh`\n";
            contents += "4. use `sudo ./install.sh` to install the client\n";
            contents += "5. use `sudo ./install.sh --remove` to uninstall it\n\n";
            contents += "Notes:\n";
            contents += "- the generated client expects a running `libElaraUI` RPC head on ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += "\n";
            contents += "- `run-ui-head.sh` defaults to `/usr/local/bin/elaraui-server`\n";
            contents += "- override that with `ELARA_UI_SERVER=/path/to/elaraui-server ./run-ui-head.sh`\n";
            if (options.ui_client_language == ProjectOptions::UI_CLIENT_PYTHON) {
                contents += "- the Python client vendors a local copy of the flat builder and RPC client helpers\n";
                if (options.include_python_multi_cpu_template) {
                    contents += "- the generated project includes a `workers/worker_template.py` starter and `elara_ui.multi_cpu` helper that uses the installed `elara_threads` package\n";
                }
            } else {
                contents += "- the C++ client uses `ElaraUiDocumentBuilder` and `ElaraUiRpcPeer`\n";
                contents += "- if the project lives outside this repo, set `ELARA_ROOT=/path/to/elara/build ./build.sh`\n";
            }
            contents += "Use `ELARA_AGENT_API.md` as the local reference document for AI-driven edits and code generation.\n";
            return contents;
        }

        String contents;

        contents += "# ";
        contents += options.project_name;
        contents += "\n\n";
        contents += "Generated by `elara-project-builder`.\n\n";
        contents += "Selected features:\n";
        contents += "- REPL client: ";
        contents += options.include_repl ? "yes\n" : "no\n";
        contents += "- Socket mode: ";
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "server\n";
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            contents += "client\n";
        } else {
            contents += "disabled\n";
        }
        if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
            contents += "- Socket transport: ";
            contents += options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC ? "json-rpc\n" : "plain\n";
            contents += "- Socket address: ";
            contents += options.socket_address;
            contents += "\n";
            contents += "- Socket port: ";
            contents += String(options.socket_port);
            contents += "\n";
        }
        contents += "- Thread pool: ";
        contents += options.include_thread_pool ? "yes\n" : "no\n";
        contents += "- Debug harness and artifacts: ";
        contents += options.include_debug_harness ? "yes\n" : "no\n";
        contents += "- Threaded worker: ";
        contents += options.include_threaded_worker ? "yes\n" : "no\n";
        if (options.include_threaded_worker) {
            contents += "- Worker class: ";
            contents += options.worker_name;
            contents += "\n";
        }
        contents += "- IndexedDataStore skeleton: ";
        contents += options.include_indexed_data_store ? "yes\n" : "no\n";
        if (options.include_indexed_data_store) {
            contents += "- IndexedDataStore path: ";
            contents += options.indexed_data_store_path;
            contents += "\n";
            contents += "- IndexedDataStore bank-map redundancy: ";
            contents += String(options.indexed_data_store_bank_map_redundancy);
            contents += "\n";
        }
        contents += "\nBuild steps:\n";
        contents += "1. `./build.sh`\n";
        if (options.include_debug_harness) {
            contents += "2. `./debug.sh` to run validator tests with artifacts\n";
            contents += "3. `./stress.sh 30` to run timed stress tests with artifacts\n";
            contents += "4. `./fuzz.sh 30` to run timed fuzz tests with artifacts\n";
            contents += "5. `sudo ./install.sh` to install the already-built binary under `/usr/local`\n";
            contents += "6. `sudo ./install.sh --remove` to remove the installed binary\n";
            contents += "7. install state is tracked in `/usr/local/share/";
        } else {
            contents += "2. `sudo ./install.sh` to install the already-built binary under `/usr/local`\n";
            contents += "3. `sudo ./install.sh --remove` to remove the installed binary\n";
            contents += "4. install state is tracked in `/usr/local/share/";
        }
        contents += options.target_name;
        contents += "/install-manifest.txt` by default\n";
        contents += "\nBuild profiles:\n";
        contents += "- `make BUILD_PROFILE=release` for fast optimized builds\n";
        contents += "- `make BUILD_PROFILE=debug` for easier debugging\n";
        contents += "- `make BUILD_PROFILE=asan` for address/UB sanitizer runs\n";
        contents += "- `BUILD_PROFILE=debug ./build.sh` and `BUILD_PROFILE=debug ./install.sh` use the same profile via the helper scripts\n";
        contents += "- `install.sh` does not build; it expects artifacts from `./build.sh`\n";
        if (options.include_debug_harness) {
            contents += "- `debug.sh`, `stress.sh`, and `fuzz.sh` create structured artifacts under `./artifacts`\n";
            contents += "- `make debug`, `make stress`, and `make fuzz` invoke the same generated test runner directly\n";
            contents += "- use `ARTIFACT_ROOT=/path ./debug.sh` to redirect artifacts elsewhere\n";
        }
        contents += "\nLinting:\n";
        contents += "- `./build.sh` and `make` now run `elara.cpp-lint` before any compilation starts\n";
        contents += "- a lint failure blocks object compilation and linking entirely\n";
        contents += "- `make lint` runs the same strict lint pass explicitly against `./src`\n";
        contents += "- the generated project prefers `../build/bin/elara.cpp-lint` when it exists, then falls back to `/usr/local/bin/elara.cpp-lint`\n";
        contents += "- if you want to force the system install, use `ELARA_CPP_LINT=/usr/local/bin/elara.cpp-lint make lint`\n";
        contents += "- override this with `ELARA_CPP_LINT=/path/to/elara.cpp-lint make lint` if needed\n";
        contents += "- current policy allows primitives, safe Elara value types (`String`, `Memory`, `ByteArray`), plus `Ref`, `RefArray`, and `elara::threading::memory::Ref`\n";
        contents += "- permissable rules are expected to evolve over time as the framework policy is refined\n";
        contents += "\nBy default the project uses Elara staged at `../build` for includes, libraries, and tools.\n";
        contents += "Override this with `ELARA_ROOT=/path/to/elara/build make` or `ELARA_ROOT=/usr/local make` if needed.\n";
        contents += "Use `ELARA_AGENT_API.md` as the local reference document for AI-driven edits and code generation.\n";
        return contents;
    }

    String ProjectBuilder::renderTestMainCpp(const ProjectOptions &options) {
        String contents;
        String tests_name = projectClassPrefix(options.project_name) + "DebugTests";

        contents += "#include <stdio.h>\n";
        contents += "#include <stdlib.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <libelaradebug/UnitTests.h>\n";
        contents += "#include \"";
        contents += tests_name;
        contents += ".h\"\n\n";
        contents += "using namespace elara;\n\n";
        contents += "int main() {\n";
        contents += "    String mode(\"validator\");\n";
        contents += "    long long duration_us = 30000000LL;\n";
        contents += "    String artifact_root(\"./artifacts\");\n";
        contents += "    String mode_env = String(getenv(\"ELARA_DEBUG_MODE\") ? getenv(\"ELARA_DEBUG_MODE\") : \"\");\n";
        contents += "    String duration_env = String(getenv(\"ELARA_DEBUG_DURATION_SECONDS\") ? getenv(\"ELARA_DEBUG_DURATION_SECONDS\") : \"\");\n";
        contents += "    String artifact_env = String(getenv(\"ELARA_DEBUG_ARTIFACT_ROOT\") ? getenv(\"ELARA_DEBUG_ARTIFACT_ROOT\") : \"\");\n";
        contents += "    if (mode_env.length()) {\n";
        contents += "        mode = mode_env;\n";
        contents += "    }\n";
        contents += "    if (duration_env.length()) {\n";
        contents += "        duration_us = ((long long)atoll(duration_env.operator char *())) * 1000000LL;\n";
        contents += "    }\n";
        contents += "    if (artifact_env.length()) {\n";
        contents += "        artifact_root = artifact_env;\n";
        contents += "    }\n\n";
        contents += "    UnitTests tests;\n";
        contents += "    tests.setArtifactRoot(artifact_root);\n";
        contents += "    tests.addRunMetadata(String(\"project\"), String(\"";
        contents += options.project_name;
        contents += "\"));\n";
        contents += "    tests.addRunMetadata(String(\"target\"), String(\"";
        contents += options.target_name;
        contents += "\"));\n";
        contents += "    tests.addRunMetadata(String(\"artifact_root\"), artifact_root);\n";
        contents += "    register";
        contents += tests_name;
        contents += "(tests);\n\n";
        contents += "    bool success = false;\n";
        contents += "    if (mode == String(\"validator\") || mode == String(\"debug\")) {\n";
        contents += "        tests.setRunMode(String(\"";
        contents += options.target_name;
        contents += "-validator\"));\n";
        contents += "        success = tests.run();\n";
        contents += "    } else {\n";
        contents += "        if (mode == String(\"stress\")) {\n";
        contents += "        tests.setRunMode(String(\"";
        contents += options.target_name;
        contents += "-stress\"));\n";
        contents += "        success = tests.runStress(duration_us);\n";
        contents += "        } else {\n";
        contents += "            if (mode == String(\"fuzz\")) {\n";
        contents += "        tests.setRunMode(String(\"";
        contents += options.target_name;
        contents += "-fuzz\"));\n";
        contents += "        success = tests.runFuzz(duration_us);\n";
        contents += "            } else {\n";
        contents += "        fprintf(stderr, \"Unknown mode: %s\\n\", mode.operator char *());\n";
        contents += "        return 1;\n";
        contents += "            }\n";
        contents += "        }\n";
        contents += "    }\n\n";
        contents += "    if (tests.getArtifactDirectory().length()) {\n";
        contents += "        printf(\"Artifacts: %s\\n\", tests.getArtifactDirectory().operator char *());\n";
        contents += "    }\n";
        contents += "    return success ? 0 : 1;\n";
        contents += "}\n";
        return contents;
    }

    String ProjectBuilder::renderDebugTestsHeader(const ProjectOptions &options) {
        String contents;
        String tests_name = projectClassPrefix(options.project_name) + "DebugTests";

        contents += "#ifndef ";
        contents += tests_name;
        contents += "_h\n";
        contents += "#define ";
        contents += tests_name;
        contents += "_h\n\n";
        contents += "#include <libelaradebug/UnitTests.h>\n\n";
        contents += "using elara::UnitTests;\n\n";
        contents += "void register";
        contents += tests_name;
        contents += "(UnitTests &tests);\n\n";
        contents += "#endif\n";
        return contents;
    }

    String ProjectBuilder::renderDebugTestsCpp(const ProjectOptions &options) {
        String contents;
        String tests_name = projectClassPrefix(options.project_name) + "DebugTests";

        contents += "#include \"";
        contents += tests_name;
        contents += ".h\"\n\n";
        contents += "#include <stdlib.h>\n";
        contents += "#include <sys/time.h>\n";
        contents += "#include <libelaracore/memory/ByteArray.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n\n";
        contents += "using namespace elara;\n\n";
        contents += "namespace {\n";
        contents += "    long long currentMicros() {\n";
        contents += "        struct timeval tv;\n";
        contents += "        gettimeofday(&tv, 0);\n";
        contents += "        return ((long long)tv.tv_sec * 1000000LL) + tv.tv_usec;\n";
        contents += "    }\n\n";
        contents += "    bool validatorBasic() {\n";
        contents += "        String value(\"debug-ready\");\n";
        contents += "        return value.trim().length() == 11;\n";
        contents += "    }\n\n";
        contents += "    bool stressStrings(long long duration_us) {\n";
        contents += "        long long end_time = currentMicros() + duration_us;\n";
        contents += "        unsigned long long iterations = 0;\n";
        contents += "        String current;\n";
        contents += "        while (currentMicros() < end_time) {\n";
        contents += "            current = String(\"stress-\") + String(iterations);\n";
        contents += "            if (!current.length()) {\n";
        contents += "                return UnitTests::fail(\"stress string build failed\");\n";
        contents += "            }\n";
        contents += "            iterations++;\n";
        contents += "        }\n";
        contents += "        return iterations > 0;\n";
        contents += "    }\n\n";
        contents += "    bool fuzzByteArray(long long duration_us) {\n";
        contents += "        long long end_time = currentMicros() + duration_us;\n";
        contents += "        ByteArray bytes;\n";
        contents += "        unsigned long long iterations = 0;\n";
        contents += "        srand(1);\n";
        contents += "        while (currentMicros() < end_time) {\n";
        contents += "            int op = rand() % 3;\n";
        contents += "            if (op == 0) {\n";
        contents += "                bytes.append((char)('a' + (rand() % 26)));\n";
        contents += "            } else {\n";
        contents += "                if (op == 1 && bytes.length()) {\n";
        contents += "                    bytes = bytes.subBytes(0, bytes.length() - 1);\n";
        contents += "                } else {\n";
        contents += "                    int original_length = 0;\n";
        contents += "                    original_length = bytes.length();\n";
        contents += "                    ByteArray copy = ByteArray(bytes);\n";
        contents += "                    if (copy.length() != original_length) {\n";
        contents += "                        return UnitTests::fail(\"bytearray length mismatch\");\n";
        contents += "                    }\n";
        contents += "                }\n";
        contents += "            }\n";
        contents += "            iterations++;\n";
        contents += "        }\n";
        contents += "        return iterations > 0;\n";
        contents += "    }\n";
        contents += "}\n\n";
        contents += "void register";
        contents += tests_name;
        contents += "(UnitTests &tests) {\n";
        contents += "    tests.addValidatorTest(String(\"generated.validator.basic\"), validatorBasic);\n";
        contents += "    tests.addStressTest(String(\"generated.stress.strings\"), stressStrings);\n";
        contents += "    tests.addFuzzTest(String(\"generated.fuzz.bytearray\"), fuzzByteArray);\n";
        contents += "}\n";
        return contents;
    }

    String ProjectBuilder::loadAgentReference() {
        String root_document = readTextFile("./ELARA_AGENT_API.md");
        if (root_document.length()) {
            return root_document;
        }

        String asset = loadAsset("ELARA_AGENT_API.md");
        if (asset.length()) {
            return asset;
        }

        return String("# Elara Agent API Reference\n\nAgent reference asset not found beside the project builder installation.\n");
    }

    String ProjectBuilder::loadAsset(String relative_path) {
        String executable_dir = pathDirectory(executable_path);
        String asset;

        if (executable_dir.length() && executable_dir != String(".")) {
            asset = readTextFile(joinPath(joinPath(joinPath(executable_dir, ".."), "share/elara-project-builder"), relative_path));
            if (asset.length())
                return asset;

            asset = readTextFile(joinPath(joinPath(executable_dir, "elara-project-builder-assets"), relative_path));
            if (asset.length())
                return asset;

            asset = readTextFile(joinPath(joinPath(executable_dir, ".."), joinPath("assets", relative_path)));
            if (asset.length())
                return asset;
        }

        asset = readTextFile(joinPath("./ElaraProjectBuilder/assets", relative_path));
        return asset;
    }

    String ProjectBuilder::readTextFile(String path) {
        struct stat st;

        if (stat(path.operator char *(), &st) != 0 || !S_ISREG(st.st_mode)) {
            return String();
        }

        FILE *fp = fopen(path.operator char *(), "rb");
        if (!fp) {
            return String();
        }

        size_t size = (size_t)st.st_size;
        char *buffer = new char[size + 1];
        size_t read_length = fread(buffer, 1, size, fp);
        fclose(fp);
        buffer[read_length] = 0;

        String contents(buffer, read_length);
        delete [] buffer;
        return contents;
    }

    bool ProjectBuilder::loadSavedProjectOptions(String output_directory, ProjectOptions *options) {
        if (!options || !output_directory.length()) {
            return false;
        }

        String config_path = joinPath(output_directory, ".elara-project-builder.config");
        String config = readTextFile(config_path);

        if (!config.length()) {
            return false;
        }

        String config_copy = config;
        StringList lines(config_copy, String("\n"));

        for (unsigned int i = 0; i < lines.length(); i++) {
            String line = lines[i].trim();
            int eq = line.indexOf(String("="));
            String key;
            String value;

            if (!line.length() || line.startsWith(String("#")) || eq < 0) {
                continue;
            }

            key = line.substr(0, eq).trim();
            value = line.substr(eq + 1).trim();

            if (key == String("project_name")) {
                options->project_name = value;
            } else if (key == String("target_name")) {
                options->target_name = value;
            } else if (key == String("application_kind")) {
                parseApplicationKindText(value, &options->application_kind);
            } else if (key == String("ui_client_language")) {
                parseUiClientLanguageText(value, &options->ui_client_language);
            } else if (key == String("ui_template")) {
                parseUiTemplateText(value, &options->ui_template);
            } else if (key == String("include_python_multi_cpu_template")) {
                parseBoolText(value, &options->include_python_multi_cpu_template);
            } else if (key == String("include_repl")) {
                parseBoolText(value, &options->include_repl);
            } else if (key == String("socket_mode")) {
                parseSocketModeText(value, &options->socket_mode);
            } else if (key == String("socket_transport")) {
                parseSocketTransportText(value, &options->socket_transport);
            } else if (key == String("include_debug_harness")) {
                parseBoolText(value, &options->include_debug_harness);
            } else if (key == String("include_thread_pool")) {
                parseBoolText(value, &options->include_thread_pool);
            } else if (key == String("include_threaded_worker")) {
                parseBoolText(value, &options->include_threaded_worker);
            } else if (key == String("include_indexed_data_store")) {
                parseBoolText(value, &options->include_indexed_data_store);
            } else if (key == String("worker_name")) {
                options->worker_name = value;
            } else if (key == String("socket_address")) {
                options->socket_address = value;
            } else if (key == String("socket_port")) {
                options->socket_port = atoi(value.operator char *());
            } else if (key == String("indexed_data_store_path")) {
                options->indexed_data_store_path = value;
            } else if (key == String("indexed_data_store_bank_map_redundancy")) {
                options->indexed_data_store_bank_map_redundancy = atoi(value.operator char *());
            }
        }

        return true;
    }

    String ProjectBuilder::renderUiCppMain(const ProjectOptions &options) {
        String contents;
        String title = options.project_name;
        String backend_id = String("org.elara.ui.") + options.target_name;

        contents += "#include <stdio.h>\n";
        contents += "#include <stdlib.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <libelaracore/memory/Ref.h>\n";
        contents += "#include <libelaraformat/json/types/JsonString.h>\n";
        contents += "#include <libelarauirpc/ElaraUiDocumentBuilder.h>\n";
        contents += "#include <libelarauirpc/ElaraUiRpcPeer.h>\n\n";
        contents += "using namespace elara;\n";
        contents += "using namespace elara::ui::rpc;\n\n";
        contents += "static void buildDocument(ElaraUiDocumentBuilder &ui) {\n";
        contents += "    ui.clear();\n";
        contents += "    ui.createWindow(String(\"";
        contents += title;
        contents += "\"), 1080, 760, String(\"";
        contents += backend_id;
        contents += "\"));\n";
        contents += "    ui.setThemeMode(String(\"light\"));\n";

        if (options.ui_template == ProjectOptions::UI_TEMPLATE_RICH_EDITOR) {
            contents += "    ui.createTabs(String(\"app.tabs\"));\n";
            contents += "    ui.setRootContent(String(\"app.tabs\"));\n";
            contents += "    ui.createRichTextEdit(String(\"app.editor\"), String(\"# ";
            contents += title;
            contents += "\\n\\nThis template gives you a starting point for a document-oriented editor built on libElaraUI.\\n\\n- Connect backend actions over RPC\\n- Extend the toolbar and outline tabs\\n- Use snapshots to inspect state while iterating\\n\"));\n";
            contents += "    ui.setPropertyNumber(String(\"app.editor\"), String(\"font_size\"), 14);\n";
            contents += "    ui.addTab(String(\"app.tabs\"), String(\"Editor\"), String(\"app.editor\"));\n";
            contents += "    ui.createListView(String(\"app.outline\"));\n";
            contents += "    ui.setPropertyNumber(String(\"app.outline\"), String(\"font_size\"), 14);\n";
            contents += "    ui.setSectionJson(String(\"app.outline\"), String(\"items\"), String(\"[{\\\"id\\\":\\\"draft\\\",\\\"label\\\":\\\"Draft notes\\\"},{\\\"id\\\":\\\"tasks\\\",\\\"label\\\":\\\"Editing tasks\\\"},{\\\"id\\\":\\\"publish\\\",\\\"label\\\":\\\"Publishing checklist\\\"}]\"));\n";
            contents += "    ui.addTab(String(\"app.tabs\"), String(\"Outline\"), String(\"app.outline\"));\n";
        } else {
            contents += "    ui.createTabs(String(\"app.tabs\"));\n";
            contents += "    ui.setRootContent(String(\"app.tabs\"));\n";
            contents += "    ui.createGrid(String(\"app.panel\"));\n";
            contents += "    ui.addTab(String(\"app.tabs\"), String(\"Control Panel\"), String(\"app.panel\"));\n";
            contents += "    ui.addGridColumnExact(String(\"app.panel\"), 24);\n";
            contents += "    ui.addGridColumnFill(String(\"app.panel\"));\n";
            contents += "    ui.addGridColumnExact(String(\"app.panel\"), 220);\n";
            contents += "    ui.addGridRowExact(String(\"app.panel\"), 24);\n";
            contents += "    ui.addGridRowExact(String(\"app.panel\"), 44);\n";
            contents += "    ui.addGridRowExact(String(\"app.panel\"), 44);\n";
            contents += "    ui.addGridRowExact(String(\"app.panel\"), 44);\n";
            contents += "    ui.addGridRowFill(String(\"app.panel\"));\n";
            contents += "    ui.addGridRowExact(String(\"app.panel\"), 24);\n";
            contents += "    ui.createLabel(String(\"app.title\"), String(\"";
            contents += title;
            contents += " control surface\"), 18);\n";
            contents += "    ui.createTextInput(String(\"app.endpoint\"), String(\"service endpoint\"), String(\"https://api.example.local\"));\n";
            contents += "    ui.createButton(String(\"app.refresh\"), String(\"Refresh\"), String(\"app.refresh\"));\n";
            contents += "    ui.createCheckbox(String(\"app.live\"), String(\"Live updates\"), true);\n";
            contents += "    ui.setPropertyNumber(String(\"app.live\"), String(\"font_size\"), 14);\n";
            contents += "    ui.createSpinner(String(\"app.interval\"), 1, 60, 5, 1);\n";
            contents += "    ui.setPropertyNumber(String(\"app.interval\"), String(\"font_size\"), 14);\n";
            contents += "    ui.createSlider(String(\"app.risk\"), String(\"horizontal\"), 0, 100, 35, 1);\n";
            contents += "    ui.createListView(String(\"app.activity\"));\n";
            contents += "    ui.setPropertyNumber(String(\"app.activity\"), String(\"font_size\"), 14);\n";
            contents += "    ui.setSectionJson(String(\"app.activity\"), String(\"items\"), String(\"[{\\\"id\\\":\\\"queued\\\",\\\"label\\\":\\\"Queued refresh\\\"},{\\\"id\\\":\\\"connected\\\",\\\"label\\\":\\\"Connected to RPC head\\\"},{\\\"id\\\":\\\"ready\\\",\\\"label\\\":\\\"Ready for backend logic\\\"}]\"));\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.title\"), 1, 1, 2, 1);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.endpoint\"), 1, 2);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.refresh\"), 2, 2);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.live\"), 1, 3);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.interval\"), 2, 3);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.risk\"), 1, 4, 2, 1);\n";
            contents += "    ui.placeGridChild(String(\"app.panel\"), String(\"app.activity\"), 1, 5, 2, 1);\n";
        }

        contents += "}\n\n";
        contents += "static bool loadDocument(ElaraUiRpcPeer *peer, const String &document_json) {\n";
        contents += "    String params = String(\"{\\\"document\\\":\") + JsonString(document_json, true).toString() + String(\"}\");\n";
        contents += "    String result_json;\n";
        contents += "    String error_code;\n";
        contents += "    String error_message;\n";
        contents += "    if (!peer->call(String(\"ui.loadDocument\"), params, result_json, error_code, error_message, 5000)) {\n";
        contents += "        printf(\"ui.loadDocument failed [%s]: %s\\n\", error_code.operator char *(), error_message.operator char *());\n";
        contents += "        return false;\n";
        contents += "    }\n";
        contents += "    printf(\"Document loaded: %s\\n\", result_json.operator char *());\n";
        contents += "    return true;\n";
        contents += "}\n\n";
        contents += "int main(int argc, const char *argv[]) {\n";
        contents += "    String host(\"";
        contents += options.socket_address;
        contents += "\");\n";
        contents += "    int port = ";
        contents += String(options.socket_port);
        contents += ";\n";
        contents += "    if (argc > 1) host = String(argv[1]);\n";
        contents += "    if (argc > 2) port = atoi(argv[2]);\n";
        contents += "    Ref<ElaraUiRpcPeer> peer(new ElaraUiRpcPeer());\n";
        contents += "    if (!peer->connect(host, (unsigned short)port)) {\n";
        contents += "        printf(\"Failed to connect to %s:%d\\n\", host.operator char *(), port);\n";
        contents += "        return 1;\n";
        contents += "    }\n";
        contents += "    ElaraUiDocumentBuilder ui;\n";
        contents += "    buildDocument(ui);\n";
        contents += "    if (!loadDocument(peer.getPtr(), ui.toJson())) {\n";
        contents += "        return 1;\n";
        contents += "    }\n";
        contents += "    printf(\"Commands: reload, snapshot, quit\\n\");\n";
        contents += "    char line[256];\n";
        contents += "    while (true) {\n";
        contents += "        printf(\"";
        contents += options.target_name;
        contents += "> \");\n";
        contents += "        if (!fgets(line, sizeof(line), stdin)) {\n";
        contents += "            break;\n";
        contents += "        }\n";
        contents += "        String command(line);\n";
        contents += "        command = command.trim();\n";
        contents += "        if (command == String(\"quit\") || command == String(\"exit\")) {\n";
        contents += "            break;\n";
        contents += "        }\n";
        contents += "        if (command == String(\"reload\")) {\n";
        contents += "            buildDocument(ui);\n";
        contents += "            loadDocument(peer.getPtr(), ui.toJson());\n";
        contents += "            continue;\n";
        contents += "        }\n";
        contents += "        if (command == String(\"snapshot\")) {\n";
        contents += "            String result_json;\n";
        contents += "            String error_code;\n";
        contents += "            String error_message;\n";
        contents += "            if (peer->call(String(\"ui.snapshot\"), String(\"{}\"), result_json, error_code, error_message, 5000)) {\n";
        contents += "                printf(\"%s\\n\", result_json.operator char *());\n";
        contents += "            } else {\n";
        contents += "                printf(\"ui.snapshot failed [%s]: %s\\n\", error_code.operator char *(), error_message.operator char *());\n";
        contents += "            }\n";
        contents += "            continue;\n";
        contents += "        }\n";
        contents += "        printf(\"Unhandled command: %s\\n\", command.operator char *());\n";
        contents += "    }\n";
        contents += "    peer->close();\n";
        contents += "    return 0;\n";
        contents += "}\n";
        return contents;
    }

    String ProjectBuilder::renderUiPythonApp(const ProjectOptions &options) {
        String contents;
        contents += "import argparse\n";
        contents += "import json\n";
        contents += "import time\n";
        contents += "from pathlib import Path\n\n";
        contents += "from elara_ui.builder import UiDocumentBuilder\n";
        contents += "from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError\n\n\n";
        contents += "def build_document():\n";
        contents += "    ui = UiDocumentBuilder()\n";
        contents += "    ui.create_window(";
        contents += String("\"") + options.project_name + "\", 1080, 760, \"" + String("org.elara.ui.") + options.target_name + "\")\n";
        contents += "    ui.set_theme_mode(\"light\")\n";

        if (options.ui_template == ProjectOptions::UI_TEMPLATE_RICH_EDITOR) {
            contents += "    ui.create_grid(\"app.shell\")\n";
            contents += "    ui.add_grid_column_fill(\"app.shell\")\n";
            contents += "    ui.add_grid_row_exact(\"app.shell\", 32)\n";
            contents += "    ui.add_grid_row_fill(\"app.shell\")\n";
            contents += "    ui.set_root_content(\"app.shell\")\n";
            contents += "    ui.create_menu_bar(\"app.menu\")\n";
            contents += "    ui.set_property_number(\"app.menu\", \"font_size\", 14)\n";
            contents += "    ui.add_menu_bar_menu(\"app.menu\", \"file\", \"File\")\n";
            contents += "    ui.add_menu_bar_item(\"app.menu\", \"file\", \"file.new\", \"New\")\n";
            contents += "    ui.add_menu_bar_item(\"app.menu\", \"file\", \"file.open\", \"Open...\")\n";
            contents += "    ui.add_menu_bar_item(\"app.menu\", \"file\", \"file.save\", \"Save\")\n";
            contents += "    ui.add_menu_bar_item(\"app.menu\", \"file\", \"file.save_as\", \"Save As...\")\n";
            contents += "    ui.add_menu_bar_item(\"app.menu\", \"file\", \"file.exit\", \"Exit\")\n";
            contents += "    ui.create_tabs(\"app.tabs\")\n";
            contents += "    ui.create_rich_text_edit(\"app.editor\", \"# ";
            contents += options.project_name;
            contents += "\\n\\nThis template gives you a starting point for a document-oriented editor built on libElaraUI.\\n\\n- Connect backend actions over RPC\\n- Extend the toolbar and outline tabs\\n- Use snapshots to inspect state while iterating\\n\")\n";
            contents += "    ui.set_property_number(\"app.editor\", \"font_size\", 14)\n";
            contents += "    ui.add_tab(\"app.tabs\", \"Editor\", \"app.editor\")\n";
            contents += "    ui.create_list_view(\"app.outline\")\n";
            contents += "    ui.set_property_number(\"app.outline\", \"font_size\", 14)\n";
            contents += "    ui.set_section_json(\"app.outline\", \"items\", [{\"id\": \"draft\", \"label\": \"Draft notes\"}, {\"id\": \"tasks\", \"label\": \"Editing tasks\"}, {\"id\": \"publish\", \"label\": \"Publishing checklist\"}])\n";
            contents += "    ui.add_tab(\"app.tabs\", \"Outline\", \"app.outline\")\n";
            contents += "    ui.place_grid_child(\"app.shell\", \"app.menu\", 0, 0)\n";
            contents += "    ui.place_grid_child(\"app.shell\", \"app.tabs\", 0, 1)\n";
        } else {
            contents += "    ui.create_tabs(\"app.tabs\")\n";
            contents += "    ui.set_root_content(\"app.tabs\")\n";
            contents += "    ui.create_grid(\"app.panel\")\n";
            contents += "    ui.add_tab(\"app.tabs\", \"Control Panel\", \"app.panel\")\n";
            contents += "    ui.add_grid_column_exact(\"app.panel\", 24)\n";
            contents += "    ui.add_grid_column_fill(\"app.panel\")\n";
            contents += "    ui.add_grid_column_exact(\"app.panel\", 220)\n";
            contents += "    ui.add_grid_row_exact(\"app.panel\", 24)\n";
            contents += "    ui.add_grid_row_exact(\"app.panel\", 44)\n";
            contents += "    ui.add_grid_row_exact(\"app.panel\", 44)\n";
            contents += "    ui.add_grid_row_exact(\"app.panel\", 44)\n";
            contents += "    ui.add_grid_row_fill(\"app.panel\")\n";
            contents += "    ui.add_grid_row_exact(\"app.panel\", 24)\n";
            contents += "    ui.create_label(\"app.title\", \"";
            contents += options.project_name;
            contents += " control surface\", 18)\n";
            contents += "    ui.create_text_input(\"app.endpoint\", \"service endpoint\", \"https://api.example.local\")\n";
            contents += "    ui.create_button(\"app.refresh\", \"Refresh\", \"app.refresh\")\n";
            contents += "    ui.create_checkbox(\"app.live\", \"Live updates\", True).set_property_number(\"app.live\", \"font_size\", 14)\n";
            contents += "    ui.create_spinner(\"app.interval\", 1, 60, 5, 1).set_property_number(\"app.interval\", \"font_size\", 14)\n";
            contents += "    ui.create_slider(\"app.risk\", \"horizontal\", 0, 100, 35, 1)\n";
            contents += "    ui.create_list_view(\"app.activity\")\n";
            contents += "    ui.set_property_number(\"app.activity\", \"font_size\", 14)\n";
            contents += "    ui.set_section_json(\"app.activity\", \"items\", [{\"id\": \"queued\", \"label\": \"Queued refresh\"}, {\"id\": \"connected\", \"label\": \"Connected to RPC head\"}, {\"id\": \"ready\", \"label\": \"Ready for backend logic\"}])\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.title\", 1, 1, 2, 1)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.endpoint\", 1, 2)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.refresh\", 2, 2)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.live\", 1, 3)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.interval\", 2, 3)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.risk\", 1, 4, 2, 1)\n";
            contents += "    ui.place_grid_child(\"app.panel\", \"app.activity\", 1, 5, 2, 1)\n";
        }

        contents += "    return ui\n\n\n";
        if (options.include_python_multi_cpu_template) {
            contents += "def start_background_worker():\n";
            contents += "    from elara_ui.multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime\n";
            contents += "    ensure_multi_cpu_runtime(thread_count=2)\n";
            contents += "    worker = MultiCpuWorkerTemplate(str(Path(__file__).resolve().parent / \"workers\" / \"worker_template.py\"), [\"8\"])\n";
            contents += "    worker.start()\n";
            contents += "    return worker\n\n\n";
        }
        contents += "def main():\n";
        contents += "    parser = argparse.ArgumentParser(description=\"Load the generated Elara UI document into a running RPC head\")\n";
        contents += "    parser.add_argument(\"--host\", default=\"";
        contents += options.socket_address;
        contents += "\", help=\"RPC server host\")\n";
        contents += "    parser.add_argument(\"--port\", default=";
        contents += String(options.socket_port);
        contents += ", type=int, help=\"RPC server port\")\n";
        contents += "    parser.add_argument(\"--snapshot\", action=\"store_true\", help=\"Fetch a root snapshot after loading\")\n";
        contents += "    parser.add_argument(\"--output\", help=\"Write the generated document JSON to this path\")\n";
        contents += "    parser.add_argument(\"--once\", action=\"store_true\", help=\"Load once and exit immediately\")\n";
        contents += "    parser.add_argument(\"--no-events\", action=\"store_true\", help=\"Do not subscribe to default UI events\")\n";
        if (options.include_python_multi_cpu_template) {
            contents += "    parser.add_argument(\"--no-worker\", action=\"store_true\", help=\"Do not start the optional multi-core worker template\")\n";
        }
        contents += "    args = parser.parse_args()\n\n";
        contents += "    def on_ui_event(params):\n";
        contents += "        print(json.dumps({\"ui.event\": params}, indent=2), flush=True)\n";
        contents += "        return {\"received\": True}\n\n";
        contents += "    builder = build_document()\n";
        contents += "    document_json = builder.to_json(indent=2)\n";
        contents += "    if args.output:\n";
        contents += "        output_path = Path(args.output)\n";
        contents += "        output_path.parent.mkdir(parents=True, exist_ok=True)\n";
        contents += "        output_path.write_text(document_json, encoding=\"utf-8\")\n";
        if (options.include_python_multi_cpu_template) {
            contents += "    worker = None\n";
        }
        contents += "    try:\n";
        contents += "        with ElaraUiRpcClient(args.host, args.port) as client:\n";
        contents += "            client.add_handler(\"ui.event\", on_ui_event)\n";
        contents += "            load_result = client.load_document(builder)\n";
        contents += "            print(json.dumps(load_result, indent=2))\n";
        contents += "            if args.snapshot:\n";
        contents += "                snapshot = client.snapshot()\n";
        contents += "                print(json.dumps(snapshot, indent=2))\n";
        contents += "            if not args.no_events:\n";
        contents += "                for action in (\"clicked\", \"keysTyped\", \"valueChanged\", \"keyDown\", \"keyUp\", \"action\"):\n";
        contents += "                    client.enable_event(action)\n";
        contents += "            if args.once:\n";
        contents += "                return\n";
        if (options.include_python_multi_cpu_template) {
            contents += "            if not args.no_worker:\n";
            contents += "                try:\n";
            contents += "                    worker = start_background_worker()\n";
            contents += "                    print(json.dumps({\"multi_cpu_worker\": worker.snapshot()}, indent=2), flush=True)\n";
            contents += "                except RuntimeError as exc:\n";
            contents += "                    print(json.dumps({\"multi_cpu_worker_disabled\": str(exc)}, indent=2), flush=True)\n";
        }
        contents += "            print(\"Connected to Elara UI RPC head. Press Ctrl+C to exit.\", flush=True)\n";
        contents += "            while True:\n";
        contents += "                time.sleep(0.25)\n";
        contents += "    except KeyboardInterrupt:\n";
        if (options.include_python_multi_cpu_template) {
            contents += "        if worker is not None:\n";
            contents += "            try:\n";
            contents += "                worker.stop()\n";
            contents += "                worker.wait(timeout_ms=2000)\n";
            contents += "            except Exception:\n";
            contents += "                pass\n";
        }
        contents += "        return\n";
        contents += "    except ElaraUiRpcError as exc:\n";
        contents += "        raise SystemExit(str(exc))\n\n\n";
        if (options.include_python_multi_cpu_template) {
            contents += "    finally:\n";
            contents += "        if worker is not None:\n";
            contents += "            try:\n";
            contents += "                worker.stop()\n";
            contents += "                worker.wait(timeout_ms=2000)\n";
            contents += "            except Exception:\n";
            contents += "                pass\n\n\n";
        }
        contents += "if __name__ == \"__main__\":\n";
        contents += "    main()\n";
        return contents;
    }

    String ProjectBuilder::renderUiPythonPackageInit(const ProjectOptions &options) {
        String contents;
        contents += "from .builder import UiDocumentBuilder\n";
        contents += "from .rpc import ElaraUiRpcClient, ElaraUiRpcError\n";
        if (options.include_python_multi_cpu_template) {
            contents += "try:\n";
            contents += "    from .multi_cpu import MultiCpuWorkerTemplate, ensure_multi_cpu_runtime\n";
            contents += "except RuntimeError:\n";
            contents += "    MultiCpuWorkerTemplate = None\n";
            contents += "    ensure_multi_cpu_runtime = None\n";
        }
        contents += "\n";
        contents += "__all__ = [\n";
        contents += "    \"UiDocumentBuilder\",\n";
        contents += "    \"ElaraUiRpcClient\",\n";
        contents += "    \"ElaraUiRpcError\",\n";
        if (options.include_python_multi_cpu_template) {
            contents += "    \"MultiCpuWorkerTemplate\",\n";
            contents += "    \"ensure_multi_cpu_runtime\",\n";
        }
        contents += "]\n";
        return contents;
    }

    String ProjectBuilder::renderUiPythonPackageBuilder(const ProjectOptions &options) {
        (void)options;
        String contents = loadAsset("python/builder.py");
        if (contents.length()) {
            return contents;
        }
        return readTextFile("./other_languages/python/builder.py");
    }

    String ProjectBuilder::renderUiPythonPackageRpc(const ProjectOptions &options) {
        (void)options;
        String contents = loadAsset("python/rpc.py");
        if (contents.length()) {
            return contents;
        }
        return readTextFile("./other_languages/python/rpc.py");
    }

    String ProjectBuilder::renderUiPythonWorkerTemplate(const ProjectOptions &options) {
        (void)options;
        String contents;
        contents += "#!/usr/bin/env python3\n\n";
        contents += "import json\n";
        contents += "import sys\n";
        contents += "import time\n\n\n";
        contents += "def main():\n";
        contents += "    iterations = 8\n";
        contents += "    if len(sys.argv) > 1:\n";
        contents += "        iterations = int(sys.argv[1])\n";
        contents += "    for index in range(iterations):\n";
        contents += "        print(json.dumps({\"worker\": \"template\", \"tick\": index}), flush=True)\n";
        contents += "        time.sleep(0.15)\n\n\n";
        contents += "if __name__ == \"__main__\":\n";
        contents += "    main()\n";
        return contents;
    }

    String ProjectBuilder::renderUiPythonMultiCpuHelper(const ProjectOptions &options) {
        (void)options;
        String contents;
        contents += "try:\n";
        contents += "    from elara_threads import PythonProcessThread\n";
        contents += "    from elara_threads import init_pool\n";
        contents += "    _ELARA_THREADS_IMPORT_ERROR = None\n";
        contents += "except ModuleNotFoundError as exc:\n";
        contents += "    PythonProcessThread = None\n";
        contents += "    init_pool = None\n";
        contents += "    _ELARA_THREADS_IMPORT_ERROR = exc\n\n\n";
        contents += "def _require_elara_threads():\n";
        contents += "    if _ELARA_THREADS_IMPORT_ERROR is not None:\n";
        contents += "        raise RuntimeError(\n";
        contents += "            \"Optional Python multi-core support requires the installed elara_threads package\"\n";
        contents += "        ) from _ELARA_THREADS_IMPORT_ERROR\n\n\n";
        contents += "def ensure_multi_cpu_runtime(thread_count=2):\n";
        contents += "    _require_elara_threads()\n";
        contents += "    init_pool(thread_count)\n\n\n";
        contents += "class MultiCpuWorkerTemplate:\n";
        contents += "    def __init__(self, script_path, args=None, python_executable=None):\n";
        contents += "        _require_elara_threads()\n";
        contents += "        self._thread = PythonProcessThread(script_path, args=args or [], python_executable=python_executable)\n\n";
        contents += "    def start(self):\n";
        contents += "        self._thread.start()\n";
        contents += "        return self\n\n";
        contents += "    def stop(self):\n";
        contents += "        self._thread.stop()\n\n";
        contents += "    def wait(self, timeout_ms=-1):\n";
        contents += "        return self._thread.wait(timeout_ms=timeout_ms)\n\n";
        contents += "    def snapshot(self):\n";
        contents += "        return self._thread.snapshot()\n";
        return contents;
    }

    String ProjectBuilder::renderProjectBuilderConfig(const ProjectOptions &options) {
        String contents;
        contents += "project_name=";
        contents += options.project_name;
        contents += "\n";
        contents += "target_name=";
        contents += options.target_name;
        contents += "\n";
        contents += "application_kind=";
        contents += applicationKindName(options.application_kind);
        contents += "\n";
        contents += "ui_client_language=";
        contents += uiClientLanguageName(options.ui_client_language);
        contents += "\n";
        contents += "ui_template=";
        contents += uiTemplateName(options.ui_template);
        contents += "\n";
        contents += "include_python_multi_cpu_template=";
        contents += boolTemplateValue(options.include_python_multi_cpu_template);
        contents += "\n";
        contents += "include_repl=";
        contents += boolTemplateValue(options.include_repl);
        contents += "\n";
        contents += "socket_mode=";
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "server";
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            contents += "client";
        } else {
            contents += "none";
        }
        contents += "\n";
        contents += "socket_transport=";
        contents += options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC ? "json-rpc" : "plain";
        contents += "\n";
        contents += "include_debug_harness=";
        contents += boolTemplateValue(options.include_debug_harness);
        contents += "\n";
        contents += "include_thread_pool=";
        contents += boolTemplateValue(options.include_thread_pool);
        contents += "\n";
        contents += "include_threaded_worker=";
        contents += boolTemplateValue(options.include_threaded_worker);
        contents += "\n";
        contents += "include_indexed_data_store=";
        contents += boolTemplateValue(options.include_indexed_data_store);
        contents += "\n";
        contents += "worker_name=";
        contents += options.worker_name;
        contents += "\n";
        contents += "socket_address=";
        contents += options.socket_address;
        contents += "\n";
        contents += "socket_port=";
        contents += String(options.socket_port);
        contents += "\n";
        contents += "indexed_data_store_path=";
        contents += options.indexed_data_store_path;
        contents += "\n";
        contents += "indexed_data_store_bank_map_redundancy=";
        contents += String(options.indexed_data_store_bank_map_redundancy);
        contents += "\n";
        return contents;
    }

    String ProjectBuilder::renderMainCpp(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        String rpc_server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String rpc_client_name = projectClassPrefix(options.project_name) + "RpcClient";
        String attrs_text;

        appendTemplateAttr(&attrs_text, options.project_name);
        appendTemplateAttr(&attrs_text, options.target_name);
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.include_repl));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.include_thread_pool));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.include_threaded_worker));
        appendTemplateAttr(&attrs_text, options.worker_name);
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_SERVER));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_CLIENT));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC));
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC));
        appendTemplateAttr(
            &attrs_text,
            boolTemplateValue(
                options.socket_mode == ProjectOptions::SOCKET_CLIENT &&
                options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC &&
                options.include_indexed_data_store
            )
        );
        appendTemplateAttr(&attrs_text, options.socket_address);
        appendTemplateAttr(&attrs_text, String(options.socket_port));
        appendTemplateAttr(&attrs_text, server_name);
        appendTemplateAttr(&attrs_text, client_name);
        appendTemplateAttr(&attrs_text, rpc_server_name);
        appendTemplateAttr(&attrs_text, rpc_client_name);
        appendTemplateAttr(&attrs_text, boolTemplateValue(options.include_indexed_data_store));
        appendTemplateAttr(&attrs_text, options.indexed_data_store_path);
        appendTemplateAttr(&attrs_text, String(options.indexed_data_store_bank_map_redundancy));

        StringList attrs(attrs_text, ";");
        return loadTemplate("main.cpp", "main", attrs);
    }

    String ProjectBuilder::renderWorkerHeader(const ProjectOptions &options) {
        StringList attrs;
        attrs.append(options.worker_name);
        return loadTemplate("Worker.h", "main", attrs);
    }

    String ProjectBuilder::renderWorkerCpp(const ProjectOptions &options) {
        StringList attrs;
        attrs.append(options.worker_name);
        return loadTemplate("Worker.cpp", "main", attrs);
    }

    String ProjectBuilder::renderSocketServerHeader(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        StringList attrs;
        attrs.append(server_name);
        return loadTemplate("SocketServer.h", "main", attrs);
    }

    String ProjectBuilder::renderSocketServerCpp(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        StringList attrs(
            String("%;%")
                .arg(server_name)
                .arg(options.project_name),
            ";"
        );
        return loadTemplate("SocketServer.cpp", "main", attrs);
    }

    String ProjectBuilder::renderSocketClientHeader(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        StringList attrs;
        attrs.append(client_name);
        return loadTemplate("SocketClient.h", "main", attrs);
    }

    String ProjectBuilder::renderSocketClientCpp(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        (void)options;
        StringList attrs;
        attrs.append(client_name);
        return loadTemplate("SocketClient.cpp", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCServerHeader(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        StringList attrs(
            String("%;%")
                .arg(server_name)
                .arg(service_name),
            ";"
        );
        return loadTemplate("RpcServer.h", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCServerCpp(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        StringList attrs(
            String("%;%")
                .arg(server_name)
                .arg(service_name),
            ";"
        );
        return loadTemplate("RpcServer.cpp", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCServiceHeader(const ProjectOptions &options) {
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        StringList attrs(
            String("%;%")
                .arg(service_name)
                .arg(boolTemplateValue(options.include_indexed_data_store)),
            ";"
        );
        return loadTemplate("RpcService.h", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCServiceCpp(const ProjectOptions &options) {
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        StringList attrs(
            String("%;%;%;%")
                .arg(service_name)
                .arg(boolTemplateValue(options.include_indexed_data_store))
                .arg(options.indexed_data_store_path)
                .arg(options.indexed_data_store_bank_map_redundancy),
            ";"
        );
        return loadTemplate("RpcService.cpp", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCClientHeader(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "RpcClient";
        StringList attrs(
            String("%;%")
                .arg(client_name)
                .arg(boolTemplateValue(options.include_indexed_data_store)),
            ";"
        );
        return loadTemplate("RpcClient.h", "main", attrs);
    }

    String ProjectBuilder::renderJsonRPCClientCpp(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "RpcClient";
        StringList attrs(
            String("%;%")
                .arg(client_name)
                .arg(boolTemplateValue(options.include_indexed_data_store)),
            ";"
        );
        return loadTemplate("RpcClient.cpp", "main", attrs);
    }

    bool ProjectBuilder::writeProjectFiles(String output_directory, const Array<PROJECT_FILE> &files) {
        if (!ensureDirectory(output_directory)) {
            return false;
        }

        for (size_t i = 0; i < files.length(); i++) {
            String output_path = joinPath(output_directory, files[i].path);
            String directory = pathDirectory(output_path);
            if (!ensureDirectory(directory)) {
                return false;
            }
            if (!writeTextFile(output_path, files[i].contents)) {
                return false;
            }
            if (files[i].path.endsWith(".sh") && chmod(output_path.operator char *(), 0755) != 0) {
                printf("Failed to mark %s executable: %s\n", output_path.operator char *(), strerror(errno));
                return false;
            }
        }

        return true;
    }

    bool ProjectBuilder::ensureDirectory(String path) {
        if (!path.length() || path == String(".")) {
            return true;
        }

        char *buffer = new char[path.length() + 1];
        memcpy(buffer, path.operator char *(), path.length() + 1);

        for (size_t i = 1; i < path.length(); i++) {
            if (buffer[i] == '/') {
                buffer[i] = 0;
                if (!createDirectory(String(buffer))) {
                    delete [] buffer;
                    return false;
                }
                buffer[i] = '/';
            }
        }

        bool result = createDirectory(String(buffer));
        delete [] buffer;
        return result;
    }

    bool ProjectBuilder::createDirectory(String path) {
        struct stat st;

        if (!path.length() || path == String(".")) {
            return true;
        }

        if (stat(path.operator char *(), &st) == 0) {
            return S_ISDIR(st.st_mode);
        }

        if (mkdir(path.operator char *(), 0755) == 0) {
            return true;
        }

        if (errno == EEXIST) {
            return true;
        }

        printf("Failed to create directory %s: %s\n", path.operator char *(), strerror(errno));
        return false;
    }

    bool ProjectBuilder::writeTextFile(String path, String contents) {
        try {
            File file(path.operator char *());
            file.truncate();
            if (contents.length()) {
                file.write(0, contents.operator char *(), contents.length());
            }
            return true;
        } catch (...) {
            printf("Failed to write %s\n", path.operator char *());
            return false;
        }
    }

    String ProjectBuilder::pathDirectory(String path) {
        int offset = -1;

        for (int i = 0; i < path.length(); i++) {
            if (path.operator char *()[i] == '/') {
                offset = i;
            }
        }

        if (offset == -1) {
            return ".";
        }

        return path.substr(0, offset);
    }

    String ProjectBuilder::joinPath(String base, String child) {
        if (!base.length()) {
            return child;
        }
        if (!child.length()) {
            return base;
        }
        if (base.endsWith("/")) {
            return base + child;
        }
        return base + "/" + child;
    }

    String ProjectBuilder::sanitizeTargetName(String value, String fallback) {
        String result;
        bool last_was_separator = true;
        bool last_was_lower_or_digit = false;

        for (int i = 0; i < value.length(); i++) {
            char ch = value.operator char *()[i];
            if (isalnum((unsigned char)ch)) {
                if (isupper((unsigned char)ch) && result.length() && !last_was_separator && last_was_lower_or_digit) {
                    result += String('-');
                }
                if (isupper((unsigned char)ch)) {
                    ch = (char)tolower((unsigned char)ch);
                }
                result += String(ch);
                last_was_separator = false;
                last_was_lower_or_digit = islower((unsigned char)ch) || isdigit((unsigned char)ch);
            } else if (!last_was_separator) {
                result += String('-');
                last_was_separator = true;
                last_was_lower_or_digit = false;
            }
        }

        while (result.length() && result.endsWith("-")) {
            result = result.substr(0, result.length() - 1);
        }

        if (!result.length()) {
            return fallback;
        }

        return result;
    }

    String ProjectBuilder::sanitizeClassName(String value, String fallback) {
        String result;
        bool upper_next = true;

        for (int i = 0; i < value.length(); i++) {
            char ch = value.operator char *()[i];
            if (isalnum((unsigned char)ch)) {
                if (!result.length() && isdigit((unsigned char)ch)) {
                    continue;
                }
                if (upper_next) {
                    ch = (char)toupper((unsigned char)ch);
                }
                result += String(ch);
                upper_next = false;
            } else {
                upper_next = true;
            }
        }

        if (!result.length()) {
            return fallback;
        }

        return result;
    }

    String ProjectBuilder::projectClassPrefix(String project_name) {
        return sanitizeClassName(project_name, "ElaraGeneratedProject");
    }

    String ProjectBuilder::buildElaraLibFlags(const ProjectOptions &options) {
        String flags;

        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            if (options.ui_client_language == ProjectOptions::UI_CLIENT_CPP) {
                flags += "-lelarauirpc -lelarasockets -lelaraformat -lelaraio -lelaradebug -lelaraevent -lelarathreads -lelaracore";
            } else {
                flags += "-lelaracore";
            }
        } else if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
            if (options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
                flags += "-lelarasockets -lelaraformat -lelaraevent -lelaradebug -lelarathreads -lelaraio -lelaracore";
            } else {
                flags += "-lelarasockets -lelaraevent -lelaradebug -lelarathreads -lelaraio -lelaracore";
            }
        } else if (options.include_indexed_data_store) {
            flags += "-lelaraio -lelaracore";
        } else if (options.include_thread_pool) {
            flags += "-lelarathreads -lelaracore";
        } else {
            flags += "-lelaracore";
        }

        return flags;
    }

    String ProjectBuilder::buildSystemLibFlags(const ProjectOptions &options) {
        String flags;

        if (options.application_kind == ProjectOptions::APPLICATION_UI) {
            if (options.ui_client_language == ProjectOptions::UI_CLIENT_CPP) {
                flags += "-pthread";
            }
        } else if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
            flags += "-levent -levent_pthreads -pthread";
        } else if (options.include_thread_pool) {
            flags += "-pthread";
        }

        return flags;
    }

}

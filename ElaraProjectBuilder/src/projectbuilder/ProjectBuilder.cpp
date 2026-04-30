#include "ProjectBuilder.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <libelaraio/File.h>

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/StringList.h>

namespace elara {

    namespace {

        String replaceTemplateMarker(String source, String marker, String value) {
            if (!marker.length())
                return source;

            std::string source_text((char*)source, (size_t)source.length());
            std::string marker_text((char*)marker, (size_t)marker.length());
            std::string value_text((char*)value, (size_t)value.length());
            std::string result;
            std::string::size_type offset = 0;
            std::string::size_type index = source_text.find(marker_text, offset);

            while (index != std::string::npos) {
                result.append(source_text, offset, index - offset);
                result.append(value_text);
                offset = index + marker_text.length();
                index = source_text.find(marker_text, offset);
            }

            result.append(source_text, offset, source_text.length() - offset);
            return String(result.c_str(), result.length());
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
        printf("  ./build.sh\n");
        if (options.include_debug_harness) {
            printf("  ./debug.sh\n");
        }
        printf("  sudo ./install.sh\n");
        return true;
    }

    String ProjectBuilder::loadTemplate(const String& template_name, const String& block_name, const StringList& attrs) {
        CodeTemplate tpl;
        tpl.loadData(loadAsset(String("templates/src/%.tpl").arg(template_name)));
        return tpl.getCode(block_name, attrs);
    }

    ProjectOptions ProjectBuilder::promptOptions() {
        ProjectOptions options = defaultOptions();
        String project_name_default = options.project_name;
        String target_name_default;
        String output_directory_default;

        printf("Elara Project Builder\n");
        printf("=====================\n");

        options.project_name = promptString("Project name", project_name_default);
        target_name_default = sanitizeTargetName(options.project_name, "elara-app");
        options.target_name = promptString("Executable name", target_name_default);

        output_directory_default = default_output_directory.length() ? default_output_directory : options.project_name;
        options.output_directory = promptString("Output directory", output_directory_default);

        options.include_repl = promptYesNo("Include REPL client", true);
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
        options.include_thread_pool = promptYesNo("Include thread pool", false);
        options.include_threaded_worker = promptYesNo("Include threaded worker class", false);
        options.include_debug_harness = promptYesNo("Include debug harness and artifacts", true);
        options.include_indexed_data_store = promptYesNo("Include IndexedDataStore skeleton", false);

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

        return options;
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
            options.socket_port = 4040;
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
        (void)options;

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
        (void)options;

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

    String ProjectBuilder::renderReadme(const ProjectOptions &options) {
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

    String ProjectBuilder::renderAssetTemplate(String relative_path, const Array<TEMPLATE_REPLACEMENT> &replacements) {
        String contents = loadAsset(relative_path);

        if (!contents.length()) {
            return String("/* Missing template asset: % */\n").arg(relative_path);
        }

        for (unsigned int i=0; i<replacements.length(); i++) {
            contents = replaceTemplateMarker(contents, replacements[i].marker, replacements[i].value);
        }

        contents = replaceTemplateMarker(contents, String("@PERCENT@"), String("%"));
        return contents;
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

    String ProjectBuilder::renderMainCpp(const ProjectOptions &options) {
        CodeTemplate tpl;
        tpl.setTemplateLoader(this);
        tpl.loadData(loadAsset(String("templates/src/main.cpp.blocks.tpl")));

        String contents;
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        String rpc_server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String rpc_client_name = projectClassPrefix(options.project_name) + "RpcClient";

        contents += "#include <stdio.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n";
        if (options.include_indexed_data_store) {
            contents += tpl.getCode("include_indexed_data_store");
        }
        if (options.include_thread_pool) {
            contents += tpl.getCode("include_thread_pool");
        }
        if (options.include_threaded_worker) {
            StringList attr(String("%").arg(options.worker_name), ";");
            contents += tpl.getCode("include_threaded_worker", attr);
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
            StringList attr(String("%").arg(client_name), ";");
            contents += tpl.getCode("include_socket", attr);
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
            StringList attr(String("%").arg(server_name), ";");
            contents += tpl.getCode("include_socket", attr);
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
            StringList attr(String("%").arg(rpc_server_name), ";");
            contents += tpl.getCode("include_socket", attr);
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
            contents += tpl.getCode("include_rpc_client");
            StringList attr(String("%").arg(rpc_server_name), ";");
            contents += tpl.getCode("include_socket", attr);
        }
        contents += "\nusing namespace elara;\n\n";
        contents += "static void printHelp() {\n";
        contents += "    printf(\"Commands:\\n\");\n";
        contents += "    printf(\"  help  - show this help\\n\");\n";
        contents += "    printf(\"  quit  - exit the application\\n\");\n";
        if (options.include_threaded_worker) {
            contents += "    printf(\"  work <text> - queue the worker task\\n\");\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "    printf(\"  status - display thread pool state\\n\");\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
            contents += "    printf(\"  send <text> - send text to the remote socket\\n\");\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
            contents += "    printf(\"  rpc connect [address] [port] - connect the JSON RPC client\\n\");\n";
            contents += "    printf(\"  rpc call <method> [params-json] - call a JSON RPC method\\n\");\n";
            contents += "    printf(\"  rpc disconnect - close the JSON RPC client socket\\n\");\n";
            contents += "    printf(\"  rpc profile - show the current command-line profile\\n\");\n";
            contents += "    printf(\"  rpc profile <rpc-default|simple> - select the invocation profile\\n\");\n";
            contents += "    printf(\"  rpc invoke <command-line> - parse and invoke a remote RPC method\\n\");\n";
            contents += "    printf(\"  ping - call echo.ping on the remote RPC server\\n\");\n";
            if (options.include_indexed_data_store) {
                contents += "    printf(\"  remote-initstore - call store.init on the remote RPC server\\n\");\n";
                contents += "    printf(\"  remote-put <key> <value> - call store.put on the remote RPC server\\n\");\n";
                contents += "    printf(\"  remote-get <key> - call store.get on the remote RPC server\\n\");\n";
            }
        }
        if (options.include_indexed_data_store) {
            contents += "    printf(\"  initstore - create or reset the IndexedDataStore file\\n\");\n";
            contents += "    printf(\"  put <key> <value> - persist a UTF-8 string value\\n\");\n";
            contents += "    printf(\"  get <key> - load and print a UTF-8 string value\\n\");\n";
        }
        contents += "}\n\n";
        if (options.include_indexed_data_store) {
            StringList attr(String("%;%").arg(options.indexed_data_store_path).arg(options.indexed_data_store_bank_map_redundancy), ";");
            contents += tpl.getCode("indexed_data_store_helpers", attr);
        }
        contents += "int main() {\n\n";
        if (options.include_thread_pool) {
            contents += "    Thread::pool = true;\n";
            contents += "    Task::staticInit();\n";
            contents += "    Thread::init(4);\n";
            contents += "    printf(\"Thread pool initialised with 4 worker threads.\\n\");\n\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
            contents += "    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());\n";
            contents += "    Socket::init(event_base.getPtr());\n";
            contents += "    Ref<";
            contents += server_name;
            contents += "> socket_server = Ref<";
            contents += server_name;
            contents += ">(new ";
            contents += server_name;
            contents += "());\n";
            contents += "    socket_server->start(";
            contents += String(options.socket_port);
            contents += ", String(\"";
            contents += options.socket_address;
            contents += "\"));\n";
            contents += "    printf(\"Socket server started on ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += ".\\n\");\n\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
            contents += "    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());\n";
            contents += "    Socket::init(event_base.getPtr());\n";
            contents += "    Ref<";
            contents += rpc_server_name;
            contents += "> socket_server = Ref<";
            contents += rpc_server_name;
            contents += ">(new ";
            contents += rpc_server_name;
            contents += "());\n";
            contents += "    socket_server->start(";
            contents += String(options.socket_port);
            contents += ", String(\"";
            contents += options.socket_address;
            contents += "\"));\n";
            contents += "    printf(\"JSON RPC server started on ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += ".\\n\");\n\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
            contents += "    Ref<EventBase> event_base = Ref<EventBase>(new EventBase());\n";
            contents += "    Socket::init(event_base.getPtr());\n";
            contents += "    event_base->runEventLoop(true);\n";
            contents += "    Ref<";
            contents += client_name;
            contents += "> socket_client = Ref<";
            contents += client_name;
            contents += ">(new ";
            contents += client_name;
            contents += "(String(\"";
            contents += options.socket_address;
            contents += "\"), ";
            contents += String(options.socket_port);
            contents += "));\n";
            contents += "    printf(\"Socket client connecting to ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += ".\\n\");\n\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
            contents += "    Ref<";
            contents += rpc_client_name;
            contents += "> socket_client = Ref<";
            contents += rpc_client_name;
            contents += ">(new ";
            contents += rpc_client_name;
            contents += "());\n";
            contents += "    String rpc_address(\"";
            contents += options.socket_address;
            contents += "\");\n";
            contents += "    int rpc_port = ";
            contents += String(options.socket_port);
            contents += ";\n";
            contents += "    String rpc_profile(\"rpc-default\");\n";
            contents += "    bool rpc_connected = false;\n";
            contents += "    if (!socket_client->connectTo(String(\"";
            contents += options.socket_address;
            contents += "\"), ";
            contents += String(options.socket_port);
            contents += ")) {\n";
            contents += "        printf(\"Failed to connect JSON RPC client to ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += ".\\n\");\n";
            contents += "    } else {\n";
            contents += "        rpc_connected = true;\n";
            contents += "        printf(\"JSON RPC client connected to ";
            contents += options.socket_address;
            contents += ":";
            contents += String(options.socket_port);
            contents += ".\\n\");\n";
            contents += "    }\n\n";
        }
        if (options.include_repl) {
            if (options.include_indexed_data_store) {
                contents += "    printf(\"IndexedDataStore path: ";
                contents += options.indexed_data_store_path;
                contents += " (bank-map redundancy ";
                contents += String(options.indexed_data_store_bank_map_redundancy);
                contents += ")\\n\");\n";
                contents += "    printf(\"Run 'initstore' before using persistence commands on a new project.\\n\");\n";
            }
            contents += "    printHelp();\n";
            contents += "    char line[1024];\n";
            contents += "    while (true) {\n";
            contents += "        printf(\"";
            contents += options.target_name;
            contents += "> \");\n";
            contents += "        if (!fgets(line, sizeof(line), stdin)) {\n";
            contents += "            break;\n";
            contents += "        }\n";
            contents += "        String command = String(line);\n";
            contents += "        command = command.trim();\n";
            contents += "        if (!command.length()) {\n";
            contents += "            continue;\n";
            contents += "        }\n";
            contents += "        if (command == String(\"help\")) {\n";
            contents += "            printHelp();\n";
            contents += "            continue;\n";
            contents += "        }\n";
            contents += "        if (command == String(\"quit\") || command == String(\"exit\")) {\n";
            contents += "            break;\n";
            contents += "        }\n";
            if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
                contents += "        if (command == String(\"status\")) {\n";
                contents += "            int total = 0;\n";
                contents += "            int active = 0;\n";
                contents += "            Thread::getThreadPoolState(&total, &active);\n";
                contents += "            printf(\"Thread pool: total=%d active=%d waiting=%d\\n\", total, active, total - active);\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
                contents += "        if (command.startsWith(\"send \")) {\n";
                contents += "            socket_client->sendLine(command.substr(5));\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
                contents += tpl.getCode("socket_client_rpc");
            }
            if (options.include_threaded_worker) {
                contents += "        if (command.startsWith(\"work \")) {\n";
                contents += "            elara::threading::memory::Ref<Task> task = elara::threading::memory::Ref<Task>(new ";
                contents += options.worker_name;
                contents += "(command.substr(5)));\n";
                contents += "            Thread::runTask(task);\n";
                contents += "            printf(\"Queued worker task.\\n\");\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            if (options.include_indexed_data_store) {
                StringList attr(String("%").arg(options.indexed_data_store_path), ";");
                contents += tpl.getCode("indexed_data_store_cli_logic", attr);
                if (options.socket_mode == ProjectOptions::SOCKET_CLIENT && options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_JSON_RPC) {
                    contents += tpl.getCode("indexed_data_store_cli_logic+json_rpc");
                }
            }
            contents += "        printf(\"Unhandled command: %s\\n\", command.operator char *());\n";
            contents += "    }\n";
        } else if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "    printf(\"Server running. Press Ctrl+C to stop.\\n\");\n";
            contents += "    while (true) {\n";
            contents += "        sleep(1);\n";
            contents += "    }\n";
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            contents += "    printf(\"Client running. Press Ctrl+C to stop.\\n\");\n";
            contents += "    while (true) {\n";
            contents += "        sleep(1);\n";
            contents += "    }\n";
        } else {
            contents += "    printf(\"";
            contents += options.project_name;
            contents += " generated successfully. Add your application logic here.\\n\");\n";
        }
        contents += "\n";
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "    socket_server->stop();\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            if (options.socket_transport == ProjectOptions::SOCKET_TRANSPORT_PLAIN) {
                contents += "    event_base->breakEventLoop();\n";
                contents += "    socket_client->close();\n";
            } else {
                contents += "    socket_client->close();\n";
            }
        }
        if (options.include_thread_pool) {
            contents += "    Thread::stopAllThreads();\n";
            contents += "    Thread::staticCleanUp();\n";
            contents += "    Task::staticCleanup();\n";
        }
        contents += "    return 0;\n";
        contents += "}\n";

        return contents;
    }

    String ProjectBuilder::renderWorkerHeader(const ProjectOptions &options) {
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%WorkerName%";
        replacement.value = options.worker_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/Worker.h.tpl", replacements);
    }

    String ProjectBuilder::renderWorkerCpp(const ProjectOptions &options) {
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%WorkerName%";
        replacement.value = options.worker_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/Worker.cpp.tpl", replacements);
    }

    String ProjectBuilder::renderSocketServerHeader(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%SocketServerName%";
        replacement.value = server_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/SocketServer.h.tpl", replacements);
    }

    String ProjectBuilder::renderSocketServerCpp(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%SocketServerName%";
        replacement.value = server_name;
        replacements.push(replacement);

        replacement.marker = "%ProjectName%";
        replacement.value = options.project_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/SocketServer.cpp.tpl", replacements);
    }

    String ProjectBuilder::renderSocketClientHeader(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%SocketClientName%";
        replacement.value = client_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/SocketClient.h.tpl", replacements);
    }

    String ProjectBuilder::renderSocketClientCpp(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%SocketClientName%";
        replacement.value = client_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/SocketClient.cpp.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCServerHeader(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%RpcServerName%";
        replacement.value = server_name;
        replacements.push(replacement);

        replacement.marker = "%RpcServiceName%";
        replacement.value = service_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcServer.h.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCServerCpp(const ProjectOptions &options) {
        String server_name = projectClassPrefix(options.project_name) + "RpcServer";
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;

        replacement.marker = "%RpcServerName%";
        replacement.value = server_name;
        replacements.push(replacement);

        replacement.marker = "%RpcServiceName%";
        replacement.value = service_name;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcServer.cpp.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCServiceHeader(const ProjectOptions &options) {
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;
        String indexed_data_store_includes;
        String indexed_data_store_private;

        if (options.include_indexed_data_store) {
            indexed_data_store_includes += "#include <libelaraio/IndexedDataStore.h>\n";
            indexed_data_store_includes += "#include <libelaracore/memory/Ref.h>\n";
            indexed_data_store_includes += "using elara::IndexedDataStore;\n";
            indexed_data_store_includes += "using elara::Memory;\n";
            indexed_data_store_includes += "using elara::Ref;\n";
            indexed_data_store_private = "\nprivate:\n    Ref<IndexedDataStore> openIndexedDataStore(bool create_if_missing);\n";
        }

        replacement.marker = "%RpcServiceName%";
        replacement.value = service_name;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStoreIncludes%";
        replacement.value = indexed_data_store_includes;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStorePrivate%";
        replacement.value = indexed_data_store_private;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcService.h.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCServiceCpp(const ProjectOptions &options) {
        String service_name = projectClassPrefix(options.project_name) + "RpcService";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;
        String indexed_data_store_headers;
        String indexed_data_store_helpers;
        String open_indexed_data_store;
        String store_method_handlers;

        if (options.include_indexed_data_store) {
            indexed_data_store_headers += "#include <sys/stat.h>\n";
            indexed_data_store_headers += "#include <sys/types.h>\n";

            indexed_data_store_helpers += "namespace {\n";
            indexed_data_store_helpers += "    void ensureDirectoryPath(String path) {\n";
            indexed_data_store_helpers += "        String current;\n";
            indexed_data_store_helpers += "        for (int i=0; i<path.length(); i++) {\n";
            indexed_data_store_helpers += "            char ch = path.operator char *()[i];\n";
            indexed_data_store_helpers += "            current += String(ch);\n";
            indexed_data_store_helpers += "            if (ch == '/') {\n";
            indexed_data_store_helpers += "                mkdir(current.operator char *(), 0755);\n";
            indexed_data_store_helpers += "            }\n";
            indexed_data_store_helpers += "        }\n";
            indexed_data_store_helpers += "    }\n\n";
            indexed_data_store_helpers += "    String parentDirectory(String path) {\n";
            indexed_data_store_helpers += "        int slash = -1;\n";
            indexed_data_store_helpers += "        for (int i=0; i<path.length(); i++) {\n";
            indexed_data_store_helpers += "            if (path.operator char *()[i] == '/') {\n";
            indexed_data_store_helpers += "                slash = i;\n";
            indexed_data_store_helpers += "            }\n";
            indexed_data_store_helpers += "        }\n";
            indexed_data_store_helpers += "        if (slash <= 0) {\n";
            indexed_data_store_helpers += "            return String();\n";
            indexed_data_store_helpers += "        }\n";
            indexed_data_store_helpers += "        return path.substr(0, slash);\n";
            indexed_data_store_helpers += "    }\n";
            indexed_data_store_helpers += "}\n\n";

            open_indexed_data_store += "Ref<IndexedDataStore> ";
            open_indexed_data_store += service_name;
            open_indexed_data_store += "::openIndexedDataStore(bool create_if_missing) {\n";
            open_indexed_data_store += "    String path(\"";
            open_indexed_data_store += options.indexed_data_store_path;
            open_indexed_data_store += "\");\n";
            open_indexed_data_store += "    if (create_if_missing) {\n";
            open_indexed_data_store += "        String directory = parentDirectory(path);\n";
            open_indexed_data_store += "        if (directory.length()) {\n";
            open_indexed_data_store += "            ensureDirectoryPath(directory + String(\"/\"));\n";
            open_indexed_data_store += "        }\n";
            open_indexed_data_store += "        return Ref<IndexedDataStore>(new IndexedDataStore(path, ";
            open_indexed_data_store += String(options.indexed_data_store_bank_map_redundancy);
            open_indexed_data_store += "));\n";
            open_indexed_data_store += "    }\n";
            open_indexed_data_store += "    return Ref<IndexedDataStore>(new IndexedDataStore(path));\n";
            open_indexed_data_store += "}\n\n";

            store_method_handlers += "    if (method == String(\"storeInit\")) {\n";
            store_method_handlers += "        try {\n";
            store_method_handlers += "            openIndexedDataStore(true);\n";
            store_method_handlers += "            result_json = String(\"{\\\"initialised\\\":true}\");\n";
            store_method_handlers += "            return true;\n";
            store_method_handlers += "        } catch (const char *error) {\n";
            store_method_handlers += "            error_code = String(\"store_init_failed\");\n";
            store_method_handlers += "            error_message = String(error);\n";
            store_method_handlers += "            return false;\n";
            store_method_handlers += "        }\n";
            store_method_handlers += "    }\n";
            store_method_handlers += "    if (method == String(\"storePut\")) {\n";
            store_method_handlers += "        String key;\n";
            store_method_handlers += "        String value;\n";
            store_method_handlers += "        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String(\"key\"), key);\n";
            store_method_handlers += "        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String(\"value\"), value);\n";
            store_method_handlers += "        if (!key.length()) {\n";
            store_method_handlers += "            error_code = String(\"missing_key\");\n";
            store_method_handlers += "            error_message = String(\"key is required\");\n";
            store_method_handlers += "            return false;\n";
            store_method_handlers += "        }\n";
            store_method_handlers += "        try {\n";
            store_method_handlers += "            Ref<IndexedDataStore> store = openIndexedDataStore(false);\n";
            store_method_handlers += "            store->set(Memory((char*)key, key.length()), Memory((char*)value, value.length()));\n";
            store_method_handlers += "            result_json = String(\"{\\\"stored\\\":true}\");\n";
            store_method_handlers += "            return true;\n";
            store_method_handlers += "        } catch (const char *error) {\n";
            store_method_handlers += "            error_code = String(\"store_put_failed\");\n";
            store_method_handlers += "            error_message = String(error);\n";
            store_method_handlers += "            return false;\n";
            store_method_handlers += "        }\n";
            store_method_handlers += "    }\n";
            store_method_handlers += "    if (method == String(\"storeGet\")) {\n";
            store_method_handlers += "        String key;\n";
            store_method_handlers += "        elara::sockets::rpc::json::JsonRPCCodec::getStringField(params_json, String(\"key\"), key);\n";
            store_method_handlers += "        if (!key.length()) {\n";
            store_method_handlers += "            error_code = String(\"missing_key\");\n";
            store_method_handlers += "            error_message = String(\"key is required\");\n";
            store_method_handlers += "            return false;\n";
            store_method_handlers += "        }\n";
            store_method_handlers += "        try {\n";
            store_method_handlers += "            Ref<IndexedDataStore> store = openIndexedDataStore(false);\n";
            store_method_handlers += "            Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store->getFile(Memory((char*)key, key.length()));\n";
            store_method_handlers += "            if (!file.getPtr()) {\n";
            store_method_handlers += "                error_code = String(\"not_found\");\n";
            store_method_handlers += "                error_message = String(\"No value for the requested key\");\n";
            store_method_handlers += "                return false;\n";
            store_method_handlers += "            }\n";
            store_method_handlers += "            unsigned long long len = store->getFileSize(file);\n";
            store_method_handlers += "            Memory value = store->readFromFile(file, 0, len);\n";
            store_method_handlers += "            String escaped_value = elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(String((char*)value, value.length()));\n";
            store_method_handlers += "            result_json = String(\"{\\\"value\\\":\\\"\") + escaped_value + String(\"\\\"}\");\n";
            store_method_handlers += "            return true;\n";
            store_method_handlers += "        } catch (const char *error) {\n";
            store_method_handlers += "            error_code = String(\"store_get_failed\");\n";
            store_method_handlers += "            error_message = String(error);\n";
            store_method_handlers += "            return false;\n";
            store_method_handlers += "        }\n";
            store_method_handlers += "    }\n";
        }

        replacement.marker = "%RpcServiceName%";
        replacement.value = service_name;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStoreHeaders%";
        replacement.value = indexed_data_store_headers;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStoreHelpers%";
        replacement.value = indexed_data_store_helpers;
        replacements.push(replacement);

        replacement.marker = "%OpenIndexedDataStoreMethod%";
        replacement.value = open_indexed_data_store;
        replacements.push(replacement);

        replacement.marker = "%StoreMethodHandlers%";
        replacement.value = store_method_handlers;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcService.cpp.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCClientHeader(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "RpcClient";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;
        String indexed_data_store_methods;

        if (options.include_indexed_data_store) {
            indexed_data_store_methods += "    bool storeInit(String &result_json, String &error_code, String &error_message);\n";
            indexed_data_store_methods += "    bool storePut(const String &key, const String &value, String &result_json, String &error_code, String &error_message);\n";
            indexed_data_store_methods += "    bool storeGet(const String &key, String &result_json, String &error_code, String &error_message);\n";
        }

        replacement.marker = "%RpcClientName%";
        replacement.value = client_name;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStoreMethods%";
        replacement.value = indexed_data_store_methods;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcClient.h.tpl", replacements);
    }

    String ProjectBuilder::renderJsonRPCClientCpp(const ProjectOptions &options) {
        String client_name = projectClassPrefix(options.project_name) + "RpcClient";
        Array<TEMPLATE_REPLACEMENT> replacements;
        TEMPLATE_REPLACEMENT replacement;
        String indexed_data_store_methods;

        if (options.include_indexed_data_store) {
            indexed_data_store_methods += "bool ";
            indexed_data_store_methods += client_name;
            indexed_data_store_methods += "::storeInit(String &result_json, String &error_code, String &error_message) {\n";
            indexed_data_store_methods += "    return rpc_client.call(String(\"app.storeInit\"), String(\"{}\"), result_json, error_code, error_message);\n";
            indexed_data_store_methods += "}\n\n";
            indexed_data_store_methods += "bool ";
            indexed_data_store_methods += client_name;
            indexed_data_store_methods += "::storePut(const String &key, const String &value, String &result_json, String &error_code, String &error_message) {\n";
            indexed_data_store_methods += "    String params = String(\"{\\\"key\\\":\\\"\") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(key) + String(\"\\\",\\\"value\\\":\\\"\") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(value) + String(\"\\\"}\");\n";
            indexed_data_store_methods += "    return rpc_client.call(String(\"app.storePut\"), params, result_json, error_code, error_message);\n";
            indexed_data_store_methods += "}\n\n";
            indexed_data_store_methods += "bool ";
            indexed_data_store_methods += client_name;
            indexed_data_store_methods += "::storeGet(const String &key, String &result_json, String &error_code, String &error_message) {\n";
            indexed_data_store_methods += "    String params = String(\"{\\\"key\\\":\\\"\") + elara::sockets::rpc::json::JsonRPCCodec::escapeJsonString(key) + String(\"\\\"}\");\n";
            indexed_data_store_methods += "    return rpc_client.call(String(\"app.storeGet\"), params, result_json, error_code, error_message);\n";
            indexed_data_store_methods += "}\n\n";
        }

        replacement.marker = "%RpcClientName%";
        replacement.value = client_name;
        replacements.push(replacement);

        replacement.marker = "%IndexedDataStoreClientMethods%";
        replacement.value = indexed_data_store_methods;
        replacements.push(replacement);

        return renderAssetTemplate("templates/src/RpcClient.cpp.tpl", replacements);
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

        if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
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

        if (options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
            flags += "-levent -levent_pthreads -pthread";
        } else if (options.include_thread_pool) {
            flags += "-pthread";
        }

        return flags;
    }

}

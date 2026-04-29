#include "ProjectBuilder.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libelaraio/File.h>

namespace elara {

    ProjectBuilder::ProjectBuilder() {
    }

    ProjectOptions ProjectBuilder::defaultOptions() {
        ProjectOptions options;

        options.project_name = "ElaraReplClient";
        options.target_name = sanitizeTargetName(options.project_name, "elara-app");
        options.output_directory = default_output_directory.length() ? default_output_directory : options.project_name;
        options.worker_name = projectClassPrefix(options.project_name) + "WorkerTask";
        options.socket_address = "0.0.0.0";

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
        printf("  make lint\n");
        printf("  sudo ./install.sh\n");
        return true;
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
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            options.socket_address = promptString("Socket bind address", "0.0.0.0");
            options.socket_port = atoi(promptString("Socket port", "4040").operator char *());
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            options.socket_address = promptString("Socket remote address", "127.0.0.1");
            options.socket_port = atoi(promptString("Socket port", "4040").operator char *());
        }
        options.include_thread_pool = promptYesNo("Include thread pool", false);
        options.include_threaded_worker = promptYesNo("Include threaded worker class", false);

        if (options.include_threaded_worker) {
            options.worker_name = promptString("Worker class name", projectClassPrefix(options.project_name) + "WorkerTask");
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
        addFile(files, "install.sh", renderInstallScript(options));
        addFile(files, "README.md", renderReadme(options));
        addFile(files, "ELARA_AGENT_API.md", loadAgentReference());
        addFile(files, "src/main.cpp", renderMainCpp(options));

        if (options.include_threaded_worker) {
            addFile(files, joinPath("src", options.worker_name + ".h"), renderWorkerHeader(options));
            addFile(files, joinPath("src", options.worker_name + ".cpp"), renderWorkerCpp(options));
        }

        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            String server_name = projectClassPrefix(options.project_name) + "SocketServer";
            addFile(files, joinPath("src", server_name + ".h"), renderSocketServerHeader(options));
            addFile(files, joinPath("src", server_name + ".cpp"), renderSocketServerCpp(options));
        } else if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            String client_name = projectClassPrefix(options.project_name) + "SocketClient";
            addFile(files, joinPath("src", client_name + ".h"), renderSocketClientHeader(options));
            addFile(files, joinPath("src", client_name + ".cpp"), renderSocketClientCpp(options));
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
        contents += "ELARA_CPP_LINT?=$(ELARA_BIN_DIR)/elara.cpp-lint\n";
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
        contents += "LINT_PATHS=./src\n";
        contents += "TARGET=";
        contents += options.target_name;
        contents += "\n\n";
        contents += ".PHONY: all lint install clean remove cleanconf prepare-build\n\n";
        contents += "all: prepare-build $(TARGET)\n\n";
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
        contents += "build/src/%.o: ./src/%.cpp prepare-build\n";
        contents += "\t@mkdir -p $(dir $@)\n";
        contents += "\t$(CC) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@\n\n";
        contents += "lint:\n";
        contents += "\t@if [ ! -x \"$(ELARA_CPP_LINT)\" ]; then \\\n";
        contents += "\t\techo \"Missing $(ELARA_CPP_LINT). Build ElaraCppLint locally first, install it to /usr/local, or set ELARA_CPP_LINT=/path/to/elara.cpp-lint.\"; \\\n";
        contents += "\t\texit 1; \\\n";
        contents += "\tfi\n";
        contents += "\t$(ELARA_CPP_LINT) $(LINT_PATHS)\n\n";
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
            contents += "- Socket address: ";
            contents += options.socket_address;
            contents += "\n";
            contents += "- Socket port: ";
            contents += String(options.socket_port);
            contents += "\n";
        }
        contents += "- Thread pool: ";
        contents += options.include_thread_pool ? "yes\n" : "no\n";
        contents += "- Threaded worker: ";
        contents += options.include_threaded_worker ? "yes\n" : "no\n";
        if (options.include_threaded_worker) {
            contents += "- Worker class: ";
            contents += options.worker_name;
            contents += "\n";
        }
        contents += "\nBuild steps:\n";
        contents += "1. `./build.sh`\n";
        contents += "2. `make lint`\n";
        contents += "3. `sudo ./install.sh` to install the already-built binary under `/usr/local`\n";
        contents += "4. `sudo ./install.sh --remove` to remove the installed binary\n";
        contents += "5. install state is tracked in `/usr/local/share/";
        contents += options.target_name;
        contents += "/install-manifest.txt` by default\n";
        contents += "\nBuild profiles:\n";
        contents += "- `make BUILD_PROFILE=release` for fast optimized builds\n";
        contents += "- `make BUILD_PROFILE=debug` for easier debugging\n";
        contents += "- `make BUILD_PROFILE=asan` for address/UB sanitizer runs\n";
        contents += "- `BUILD_PROFILE=debug ./build.sh` and `BUILD_PROFILE=debug ./install.sh` use the same profile via the helper scripts\n";
        contents += "- `install.sh` does not build; it expects artifacts from `./build.sh`\n";
        contents += "\nLinting:\n";
        contents += "- `make lint` runs `elara.cpp-lint` against `./src`\n";
        contents += "- the generated project expects the linter at `../build/bin/elara.cpp-lint` by default\n";
        contents += "- if you want the system install instead, use `ELARA_CPP_LINT=/usr/local/bin/elara.cpp-lint make lint`\n";
        contents += "- override this with `ELARA_CPP_LINT=/path/to/elara.cpp-lint make lint` if needed\n";
        contents += "- current policy allows primitives, safe Elara value types (`String`, `Memory`, `ByteArray`), plus `Ref`, `RefArray`, and `elara::threading::memory::Ref`\n";
        contents += "- permissable rules are expected to evolve over time as the framework policy is refined\n";
        contents += "\nBy default the project uses Elara staged at `../build` for includes, libraries, and tools.\n";
        contents += "Override this with `ELARA_ROOT=/path/to/elara/build make` or `ELARA_ROOT=/usr/local make` if needed.\n";
        contents += "Use `ELARA_AGENT_API.md` as the local reference document for AI-driven edits and code generation.\n";
        return contents;
    }

    String ProjectBuilder::loadAgentReference() {
        String executable_dir = pathDirectory(executable_path);
        String asset;

        if (executable_dir.length() && executable_dir != String(".")) {
            asset = readTextFile(joinPath(joinPath(joinPath(executable_dir, ".."), "share/elara-project-builder"), "ELARA_AGENT_API.md"));
            if (asset.length()) {
                return asset;
            }

            asset = readTextFile(joinPath(joinPath(executable_dir, "elara-project-builder-assets"), "ELARA_AGENT_API.md"));
            if (asset.length()) {
                return asset;
            }

            asset = readTextFile(joinPath(joinPath(executable_dir, ".."), "assets/ELARA_AGENT_API.md"));
            if (asset.length()) {
                return asset;
            }
        }

        asset = readTextFile("./ElaraProjectBuilder/assets/ELARA_AGENT_API.md");
        if (asset.length()) {
            return asset;
        }

        return String("# Elara Agent API Reference\n\nAgent reference asset not found beside the project builder installation.\n");
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
        String contents;
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";

        contents += "#include <stdio.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n";
        if (options.include_thread_pool) {
            contents += "#include <libelarathreads/Thread.h>\n";
            contents += "#include <libelarathreads/Task.h>\n";
        }
        if (options.include_threaded_worker || options.socket_mode != ProjectOptions::SOCKET_DISABLED) {
            contents += "#include <libelaracore/memory/Ref.h>\n";
        }
        if (options.include_threaded_worker) {
            contents += "#include \"";
            contents += options.worker_name;
            contents += ".h\"\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            contents += "#include <libelaraevent/EventBase.h>\n";
            contents += "#include <libelarasockets/Socket.h>\n";
            contents += "#include <unistd.h>\n";
            contents += "#include \"";
            contents += client_name;
            contents += ".h\"\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
            contents += "#include \"";
            contents += server_name;
            contents += ".h\"\n";
            contents += "#include <unistd.h>\n";
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
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
            contents += "    printf(\"  send <text> - send text to the remote socket\\n\");\n";
        }
        contents += "}\n\n";
        contents += "int main() {\n\n";
        if (options.include_thread_pool) {
            contents += "    Thread::pool = true;\n";
            contents += "    Task::staticInit();\n";
            contents += "    Thread::init(4);\n";
            contents += "    printf(\"Thread pool initialised with 4 worker threads.\\n\");\n\n";
        }
        if (options.socket_mode == ProjectOptions::SOCKET_SERVER) {
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
        if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
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
        if (options.include_repl) {
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
            if (options.socket_mode == ProjectOptions::SOCKET_CLIENT) {
                contents += "        if (command.startsWith(\"send \")) {\n";
                contents += "            socket_client->sendLine(command.substr(5));\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            if (options.include_threaded_worker) {
                contents += "        if (command.startsWith(\"work \")) {\n";
                contents += "            Ref<";
                contents += options.worker_name;
                contents += "> task = Ref<";
                contents += options.worker_name;
                contents += ">(new ";
                contents += options.worker_name;
                contents += "(command.substr(5)));\n";
                contents += "            Task::queueTask(task.getPtr());\n";
                contents += "            printf(\"Queued worker task.\\n\");\n";
                contents += "            continue;\n";
                contents += "        }\n";
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
            contents += "    event_base->breakEventLoop();\n";
            contents += "    socket_client->close();\n";
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
        String contents;

        contents += "#ifndef ";
        contents += options.worker_name;
        contents += "_h\n";
        contents += "#define ";
        contents += options.worker_name;
        contents += "_h\n\n";
        contents += "#include <libelarathreads/Task.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n\n";
        contents += "class ";
        contents += options.worker_name;
        contents += " : public elara::Task {\n";
        contents += "public:\n";
        contents += "    ";
        contents += options.worker_name;
        contents += "(elara::String payload);\n";
        contents += "    virtual ~";
        contents += options.worker_name;
        contents += "();\n\n";
        contents += "protected:\n";
        contents += "    virtual void run();\n\n";
        contents += "private:\n";
        contents += "    elara::String payload;\n";
        contents += "};\n\n";
        contents += "#endif\n";
        return contents;
    }

    String ProjectBuilder::renderWorkerCpp(const ProjectOptions &options) {
        String contents;

        contents += "#include \"";
        contents += options.worker_name;
        contents += ".h\"\n\n";
        contents += "#include <stdio.h>\n\n";
        contents += options.worker_name;
        contents += "::";
        contents += options.worker_name;
        contents += "(elara::String payload) : elara::Task(true) {\n";
        contents += "    this->payload = payload;\n";
        contents += "}\n\n";
        contents += options.worker_name;
        contents += "::~";
        contents += options.worker_name;
        contents += "() {\n";
        contents += "}\n\n";
        contents += "void ";
        contents += options.worker_name;
        contents += "::run() {\n";
        contents += "    printf(\"Worker received: %s\\n\", payload.operator char *());\n";
        contents += "    finished();\n";
        contents += "}\n";
        return contents;
    }

    String ProjectBuilder::renderSocketServerHeader(const ProjectOptions &options) {
        String contents;
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";

        contents += "#ifndef ";
        contents += server_name;
        contents += "_h\n";
        contents += "#define ";
        contents += server_name;
        contents += "_h\n\n";
        contents += "#include <libelarasockets/Listener.h>\n\n";
        contents += "#include <libelaracore/memory/String.h>\n\n";
        contents += "class ";
        contents += server_name;
        contents += " : public elara::Listener {\n";
        contents += "public:\n";
        contents += "    ";
        contents += server_name;
        contents += "();\n";
        contents += "    virtual ~";
        contents += server_name;
        contents += "();\n";
        contents += "    void start(int port, elara::String address);\n\n";
        contents += "protected:\n";
        contents += "    virtual void onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz);\n";
        contents += "};\n\n";
        contents += "#endif\n";
        return contents;
    }

    String ProjectBuilder::renderSocketServerCpp(const ProjectOptions &options) {
        String contents;
        String server_name = projectClassPrefix(options.project_name) + "SocketServer";

        contents += "#include \"";
        contents += server_name;
        contents += ".h\"\n\n";
        contents += "#include <arpa/inet.h>\n";
        contents += "#include <stdio.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <unistd.h>\n";
        contents += "#include <sys/socket.h>\n\n";
        contents += server_name;
        contents += "::";
        contents += server_name;
        contents += "() {\n";
        contents += "}\n\n";
        contents += server_name;
        contents += "::~";
        contents += server_name;
        contents += "() {\n";
        contents += "}\n\n";
        contents += "void ";
        contents += server_name;
        contents += "::start(int port, elara::String address) {\n";
        contents += "    unsigned int ipv4_interface = INADDR_ANY;\n";
        contents += "    if (address.length() && address != elara::String(\"0.0.0.0\") && address != elara::String(\"*\")) {\n";
        contents += "        ipv4_interface = inet_addr(address.operator char *());\n";
        contents += "    }\n";
        contents += "    listen(port, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED, ipv4_interface);\n";
        contents += "    runEventLoop(true);\n";
        contents += "}\n\n";
        contents += "void ";
        contents += server_name;
        contents += "::onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {\n";
        contents += "    (void)event_base;\n";
        contents += "    (void)addr;\n";
        contents += "    (void)addr_sz;\n";
        contents += "    send(fd, \"";
        contents += options.project_name;
        contents += " accepted your connection.\\n\", strlen(\"";
        contents += options.project_name;
        contents += " accepted your connection.\\n\"), 0);\n";
        contents += "    ::close(fd);\n";
        contents += "    printf(\"Accepted and closed a socket client.\\n\");\n";
        contents += "}\n";
        return contents;
    }

    String ProjectBuilder::renderSocketClientHeader(const ProjectOptions &options) {
        String contents;
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";

        contents += "#ifndef ";
        contents += client_name;
        contents += "_h\n";
        contents += "#define ";
        contents += client_name;
        contents += "_h\n\n";
        contents += "#include <libelarasockets/Socket.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n\n";
        contents += "class ";
        contents += client_name;
        contents += " : public elara::Socket {\n";
        contents += "public:\n";
        contents += "    ";
        contents += client_name;
        contents += "(elara::String address, int port);\n";
        contents += "    virtual ~";
        contents += client_name;
        contents += "();\n";
        contents += "    void sendLine(elara::String text);\n\n";
        contents += "protected:\n";
        contents += "    virtual void onReceive();\n";
        contents += "    virtual void onWriteReady();\n";
        contents += "};\n\n";
        contents += "#endif\n";
        return contents;
    }

    String ProjectBuilder::renderSocketClientCpp(const ProjectOptions &options) {
        String contents;
        String client_name = projectClassPrefix(options.project_name) + "SocketClient";

        contents += "#include \"";
        contents += client_name;
        contents += ".h\"\n\n";
        contents += "#include <libelarasockets/Address.h>\n";
        contents += "#include <libelaracore/memory/Memory.h>\n";
        contents += "#include <stdio.h>\n\n";
        contents += client_name;
        contents += "::";
        contents += client_name;
        contents += "(elara::String address, int port) : Socket() {\n";
        contents += "    if (!connect(elara::Address(elara::Address::ADDR, address.operator char *()), port)) {\n";
        contents += "        printf(\"Failed to connect to %s:%u\\n\", address.operator char *(), (unsigned int)port);\n";
        contents += "    }\n";
        contents += "}\n\n";
        contents += client_name;
        contents += "::~";
        contents += client_name;
        contents += "() {\n";
        contents += "}\n\n";
        contents += "void ";
        contents += client_name;
        contents += "::sendLine(elara::String text) {\n";
        contents += "    text += elara::String(\"\\n\");\n";
        contents += "    send(text.operator char *(), (size_t)text.length());\n";
        contents += "}\n\n";
        contents += "void ";
        contents += client_name;
        contents += "::onReceive() {\n";
        contents += "    elara::Memory data = read((int)available());\n";
        contents += "    if (data.length()) {\n";
        contents += "        fwrite(data.operator char *(), 1, (size_t)data.length(), stdout);\n";
        contents += "        fflush(stdout);\n";
        contents += "    }\n";
        contents += "}\n\n";
        contents += "void ";
        contents += client_name;
        contents += "::onWriteReady() {\n";
        contents += "}\n";
        return contents;
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
            flags += "-lelarasockets -lelaraevent -lelaradebug -lelarathreads -lelaraio -lelaracore";
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

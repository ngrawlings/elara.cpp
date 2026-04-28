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
        printf("  autoreconf -fi\n");
        printf("  ./configure\n");
        printf("  make\n");
        return true;
    }

    ProjectOptions ProjectBuilder::promptOptions() {
        ProjectOptions options;

        printf("Elara Project Builder\n");
        printf("=====================\n");

        options.project_name = promptString("Project name", "ElaraReplClient");
        options.target_name = promptString("Executable name", sanitizeTargetName(options.project_name, "elara-app"));

        if (default_output_directory.length()) {
            options.output_directory = promptString("Output directory", default_output_directory);
        } else {
            options.output_directory = promptString("Output directory", options.target_name);
        }

        options.include_repl = promptYesNo("Include REPL client", true);
        options.include_socket_server = promptYesNo("Include socket server", false);
        options.include_thread_pool = promptYesNo("Include thread pool", false);
        options.include_threaded_worker = promptYesNo("Include threaded worker class", false);

        if (options.include_threaded_worker) {
            options.worker_name = promptString("Worker class name", projectClassPrefix(options.project_name) + "WorkerTask");
        }

        if (options.include_socket_server && !options.include_thread_pool) {
            printf("Socket server support requires a thread pool to run alongside the REPL. Enabling it.\n");
            options.include_thread_pool = true;
        }

        if (options.include_threaded_worker && !options.include_thread_pool) {
            printf("Threaded workers require the thread pool. Enabling it.\n");
            options.include_thread_pool = true;
        }

        return options;
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

        if (options.include_socket_server || options.include_threaded_worker) {
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
        addFile(files, "README.md", renderReadme(options));
        addFile(files, "ELARA_AGENT_API.md", loadAgentReference());
        addFile(files, "src/main.cpp", renderMainCpp(options));

        if (options.include_threaded_worker) {
            addFile(files, joinPath("src", options.worker_name + ".h"), renderWorkerHeader(options));
            addFile(files, joinPath("src", options.worker_name + ".cpp"), renderWorkerCpp(options));
        }

        if (options.include_socket_server) {
            String server_name = projectClassPrefix(options.project_name) + "SocketServer";
            addFile(files, joinPath("src", server_name + ".h"), renderSocketServerHeader(options));
            addFile(files, joinPath("src", server_name + ".cpp"), renderSocketServerCpp(options));
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
        contents += "PREFIX?=$(abspath ./dist)\n";
        contents += "BIN_DIR?=$(PREFIX)/bin\n";
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
        contents += "BUILDPATH=./build\n";
        contents += "TARGET=";
        contents += options.target_name;
        contents += "\n\n";
        contents += "all: $(TARGET)\n\n";
        contents += "$(TARGET): $(OBJECTS)\n";
        contents += "\t$(CC) $(OBJECTS) $(STD_LDFLAGS) $(LDFLAGS) -o $(BUILDPATH)/$(TARGET)\n\n";
        contents += "build/src/%.o:\n";
        contents += "\t@mkdir -p $(dir $@)\n";
        contents += "\t$(CC) $(STD_CFLAGS) $(CFLAGS) ./src/$*.cpp -o $@\n\n";
        contents += "install:\n";
        contents += "\tmkdir -p $(BIN_DIR)\n";
        contents += "\tcp $(BUILDPATH)/$(TARGET) $(BIN_DIR)/$(TARGET)\n\n";
        contents += "clean:\n";
        contents += "\trm -rf $(BUILDPATH)\n\n";
        contents += "remove:\n";
        contents += "\trm -f $(BIN_DIR)/$(TARGET)\n\n";
        contents += "cleanconf:\n";
        contents += "\trm -f config.log\n";
        contents += "\trm -f config.status\n";
        contents += "\trm -f Makefile\n";
        contents += "\trm -f configure\n";

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
        contents += "- Socket server: ";
        contents += options.include_socket_server ? "yes\n" : "no\n";
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
        contents += "1. `autoreconf -fi`\n";
        contents += "2. `./configure`\n";
        contents += "3. `make`\n";
        contents += "\nBuild profiles:\n";
        contents += "- `make BUILD_PROFILE=release` for fast optimized builds\n";
        contents += "- `make BUILD_PROFILE=debug` for easier debugging\n";
        contents += "- `make BUILD_PROFILE=asan` for address/UB sanitizer runs\n";
        contents += "\nBy default the project links against Elara staged at `../build`.\n";
        contents += "Override this with `ELARA_ROOT=/path/to/elara/build make` if needed.\n";
        contents += "Use `ELARA_AGENT_API.md` as the local reference document for AI-driven edits and code generation.\n";
        return contents;
    }

    String ProjectBuilder::loadAgentReference() {
        String executable_dir = pathDirectory(executable_path);
        String asset;

        if (executable_dir.length() && executable_dir != String(".")) {
            asset = readTextFile(joinPath(joinPath(executable_dir, "elara-project-builder-assets"), "ELARA_AGENT_API.md"));
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

        contents += "#include <stdio.h>\n";
        contents += "#include <string.h>\n";
        contents += "#include <libelaracore/memory/String.h>\n";
        if (options.include_thread_pool) {
            contents += "#include <libelarathreads/Thread.h>\n";
            contents += "#include <libelarathreads/Task.h>\n";
        }
        if (options.include_threaded_worker) {
            contents += "#include \"";
            contents += options.worker_name;
            contents += ".h\"\n";
        }
        if (options.include_socket_server) {
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
        if (options.include_socket_server) {
            contents += "    printf(\"  status - display thread pool state\\n\");\n";
        }
        contents += "}\n\n";
        contents += "int main(int argc, const char *argv[]) {\n";
        contents += "    (void)argc;\n";
        contents += "    (void)argv;\n\n";
        if (options.include_thread_pool) {
            contents += "    Thread::pool = true;\n";
            contents += "    Thread::init(4);\n";
            contents += "    printf(\"Thread pool initialised with 4 worker threads.\\n\");\n\n";
        }
        if (options.include_socket_server) {
            contents += "    ";
            contents += server_name;
            contents += " socket_server;\n";
            contents += "    socket_server.start(4040);\n";
            contents += "    printf(\"Socket server started on port 4040.\\n\");\n\n";
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
            contents += "        String command(line);\n";
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
            if (options.include_socket_server) {
                contents += "        if (command == String(\"status\")) {\n";
                contents += "            int total = 0;\n";
                contents += "            int active = 0;\n";
                contents += "            Thread::getThreadPoolState(&total, &active);\n";
                contents += "            printf(\"Thread pool: total=%d active=%d waiting=%d\\n\", total, active, total - active);\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            if (options.include_threaded_worker) {
                contents += "        if (command.startsWith(\"work \")) {\n";
                contents += "            ";
                contents += options.worker_name;
                contents += " *task = new ";
                contents += options.worker_name;
                contents += "(command.substr(5));\n";
                contents += "            Task::queueTask(task);\n";
                contents += "            printf(\"Queued worker task.\\n\");\n";
                contents += "            continue;\n";
                contents += "        }\n";
            }
            contents += "        printf(\"Unhandled command: %s\\n\", command.operator char *());\n";
            contents += "    }\n";
        } else if (options.include_socket_server) {
            contents += "    printf(\"Server running. Press Ctrl+C to stop.\\n\");\n";
            contents += "    while (true) {\n";
            contents += "        sleep(1);\n";
            contents += "    }\n";
        } else {
            contents += "    printf(\"";
            contents += options.project_name;
            contents += " generated successfully. Add your application logic here.\\n\");\n";
        }
        contents += "\n";
        if (options.include_socket_server) {
            contents += "    socket_server.stop();\n";
        }
        if (options.include_thread_pool) {
            contents += "    Thread::stopAllThreads();\n";
            contents += "    Thread::staticCleanUp();\n";
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
        contents += "    void start(unsigned short port);\n\n";
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
        contents += "::start(unsigned short port) {\n";
        contents += "    listen(port, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED);\n";
        contents += "    runEventLoop(true);\n";
        contents += "}\n\n";
        contents += "void ";
        contents += server_name;
        contents += "::onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {\n";
        contents += "    (void)event_base;\n";
        contents += "    (void)addr;\n";
        contents += "    (void)addr_sz;\n";
        contents += "    const char *message = \"";
        contents += options.project_name;
        contents += " accepted your connection.\\n\";\n";
        contents += "    send(fd, message, strlen(message), 0);\n";
        contents += "    ::close(fd);\n";
        contents += "    printf(\"Accepted and closed a socket client.\\n\");\n";
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

        if (options.include_socket_server) {
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

        if (options.include_socket_server) {
            flags += "-levent -levent_pthreads -pthread";
        } else if (options.include_thread_pool) {
            flags += "-pthread";
        }

        return flags;
    }

}

#ifndef ElaraProjectBuilder_ProjectBuilder_h
#define ElaraProjectBuilder_ProjectBuilder_h

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

#include "ProjectOptions.h"

namespace elara {

    typedef struct {
        String path;
        String contents;
    } PROJECT_FILE;

    typedef struct {
        String marker;
        String value;
    } TEMPLATE_REPLACEMENT;

    class ProjectBuilder {
    public:
        ProjectBuilder();

        ProjectOptions defaultOptions();
        void setDefaultOutputDirectory(String output_directory);
        void setExecutablePath(String executable_path);

        bool runInteractive();
        bool generate(ProjectOptions options);

    private:
        String default_output_directory;
        String executable_path;

        ProjectOptions promptOptions();
        void normalizeOptions(ProjectOptions &options);

        bool promptYesNo(const char *prompt, bool default_value);
        String promptString(const char *prompt, String default_value);
        ProjectOptions::SocketMode promptSocketMode();
        ProjectOptions::SocketTransport promptSocketTransport();

        bool createProjectFiles(const ProjectOptions &options, Array<PROJECT_FILE> &files);
        void addFile(Array<PROJECT_FILE> &files, String path, String contents);

        String renderConfigureAc(const ProjectOptions &options);
        String renderMakefileIn(const ProjectOptions &options);
        String renderBuildScript(const ProjectOptions &options);
        String renderDebugScript(const ProjectOptions &options);
        String renderStressScript(const ProjectOptions &options);
        String renderFuzzScript(const ProjectOptions &options);
        String renderInstallScript(const ProjectOptions &options);
        String renderReadme(const ProjectOptions &options);
        String renderMainCpp(const ProjectOptions &options);
        String renderTestMainCpp(const ProjectOptions &options);
        String renderDebugTestsHeader(const ProjectOptions &options);
        String renderDebugTestsCpp(const ProjectOptions &options);
        String renderWorkerHeader(const ProjectOptions &options);
        String renderWorkerCpp(const ProjectOptions &options);
        String renderSocketServerHeader(const ProjectOptions &options);
        String renderSocketServerCpp(const ProjectOptions &options);
        String renderSocketClientHeader(const ProjectOptions &options);
        String renderSocketClientCpp(const ProjectOptions &options);
        String renderJsonRPCServerHeader(const ProjectOptions &options);
        String renderJsonRPCServerCpp(const ProjectOptions &options);
        String renderJsonRPCServiceHeader(const ProjectOptions &options);
        String renderJsonRPCServiceCpp(const ProjectOptions &options);
        String renderJsonRPCClientHeader(const ProjectOptions &options);
        String renderJsonRPCClientCpp(const ProjectOptions &options);
        String loadAgentReference();
        String loadAsset(String relative_path);
        String renderAssetTemplate(String relative_path, const Array<TEMPLATE_REPLACEMENT> &replacements);
        String readTextFile(String path);

        bool writeProjectFiles(String output_directory, const Array<PROJECT_FILE> &files);
        bool ensureDirectory(String path);
        bool createDirectory(String path);
        bool writeTextFile(String path, String contents);

        String pathDirectory(String path);
        String joinPath(String base, String child);

        String sanitizeTargetName(String value, String fallback);
        String sanitizeClassName(String value, String fallback);
        String projectClassPrefix(String project_name);

        String buildElaraLibFlags(const ProjectOptions &options);
        String buildSystemLibFlags(const ProjectOptions &options);
    };

}

#endif

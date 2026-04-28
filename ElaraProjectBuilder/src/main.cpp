#include "projectbuilder/ProjectBuilder.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    ProjectBuilder builder;
    builder.setExecutablePath(argv[0]);

    if (argc > 1) {
        builder.setDefaultOutputDirectory(argv[1]);
    }

    return builder.runInteractive() ? 0 : 1;
}

#include "projectbuilder/ProjectBuilder.h"

#include <limits.h>
#include <unistd.h>

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

int main(int argc, const char *argv[]) {
    ProjectBuilder builder;
    builder.setExecutablePath(resolveExecutablePath(argv[0]));

    if (argc > 1) {
        builder.setDefaultOutputDirectory(argv[1]);
    }

    return builder.runInteractive() ? 0 : 1;
}

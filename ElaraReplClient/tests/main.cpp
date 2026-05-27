#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libelaradebug/UnitTests.h>
#include "ElaraReplClientDebugTests.h"

using namespace elara;

int main() {
    String mode("validator");
    long long duration_us = 30000000LL;
    String artifact_root("./artifacts");
    String mode_env = String(getenv("ELARA_DEBUG_MODE") ? getenv("ELARA_DEBUG_MODE") : "");
    String duration_env = String(getenv("ELARA_DEBUG_DURATION_SECONDS") ? getenv("ELARA_DEBUG_DURATION_SECONDS") : "");
    String artifact_env = String(getenv("ELARA_DEBUG_ARTIFACT_ROOT") ? getenv("ELARA_DEBUG_ARTIFACT_ROOT") : "");
    if (mode_env.length()) {
        mode = mode_env;
    }
    if (duration_env.length()) {
        duration_us = ((long long)atoll(duration_env.operator char *())) * 1000000LL;
    }
    if (artifact_env.length()) {
        artifact_root = artifact_env;
    }

    UnitTests tests;
    tests.setArtifactRoot(artifact_root);
    tests.addRunMetadata(String("project"), String("ElaraReplClient"));
    tests.addRunMetadata(String("target"), String("elara-repl-client"));
    tests.addRunMetadata(String("artifact_root"), artifact_root);
    registerElaraReplClientDebugTests(tests);

    bool success = false;
    if (mode == String("validator") || mode == String("debug")) {
        tests.setRunMode(String("elara-repl-client-validator"));
        success = tests.run();
    } else {
        if (mode == String("stress")) {
        tests.setRunMode(String("elara-repl-client-stress"));
        success = tests.runStress(duration_us);
        } else {
            if (mode == String("fuzz")) {
        tests.setRunMode(String("elara-repl-client-fuzz"));
        success = tests.runFuzz(duration_us);
            } else {
        fprintf(stderr, "Unknown mode: %s\n", mode.operator char *());
        return 1;
            }
        }
    }

    if (tests.getArtifactDirectory().length()) {
        printf("Artifacts: %s\n", tests.getArtifactDirectory().operator char *());
    }
    return success ? 0 : 1;
}

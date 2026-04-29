#include <libelaracore/memory/String.h>
#include <libelaradebug/UnitTests.h>

#include <stdlib.h>

#include "runtime/RuntimeTests.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    UnitTests tests;
    String mode("validator");
    String selector("all");
    int duration_seconds = 10;

    if (argc > 1) {
        String arg1(argv[1]);

        if (arg1 == String("validator") || arg1 == String("stress") || arg1 == String("fuzz")) {
            mode = arg1;
            if (argc > 2)
                selector = String(argv[2]);
            if (argc > 3)
                duration_seconds = atoi(argv[3]);
        } else {
            selector = arg1;
        }
    }

    if (duration_seconds <= 0)
        duration_seconds = 10;

    tests.setRunMode(mode == String("validator")
        ? (selector == String("all") ? String("unit-tests") : selector)
        : String("%.%").arg(mode).arg(selector));
    tests.addRunMetadata("execution_mode", mode);
    tests.addRunMetadata("argv_mode", selector);
    if (mode != String("validator"))
        tests.addRunMetadata("duration_seconds", String(duration_seconds));

    int count = 0;
    addRuntimeMetadata(tests, selector);
    count += addRuntimeTests(tests, selector);

    int selected_count = mode == String("stress")
        ? tests.countTests(UNITTEST_KIND_STRESS)
        : (mode == String("fuzz") ? tests.countTests(UNITTEST_KIND_FUZZ) : tests.countTests(UNITTEST_KIND_VALIDATOR));

    if (!count || !selected_count) {
        printf("No tests matched selector: %s\r\n", (char*)selector);
        printf("Modes: validator, stress, fuzz\r\n");
        printf("Validator selectors: all, runtime, runtime.bytearray, runtime.hashmap, runtime.ringbuffer, runtime.base58, runtime.base64\r\n");
        printf("Stress selectors: runtime.stress, stress-memory\r\n");
        printf("Fuzz selectors: runtime.fuzz, runtime.fuzz.bytearray, runtime.fuzz.hashmap, runtime.fuzz.ringbuffer, runtime.fuzz.string, runtime.fuzz.linkedlist, runtime.fuzz.instancepool\r\n");
        return 2;
    }

    if (mode == String("stress"))
        return tests.runStress((long long)duration_seconds * 1000000LL) ? 0 : 1;

    if (mode == String("fuzz"))
        return tests.runFuzz((long long)duration_seconds * 1000000LL) ? 0 : 1;

    return tests.run() ? 0 : 1;
}

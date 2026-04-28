#include <libelaracore/memory/String.h>
#include <libelaradebug/UnitTests.h>

#include "runtime/RuntimeTests.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    UnitTests tests;
    String selector("all");

    if (argc > 1)
        selector = String(argv[1]);

    tests.setRunMode(selector == String("all") ? String("unit-tests") : selector);
    tests.addRunMetadata("argv_mode", selector);

    int count = 0;
    addRuntimeMetadata(tests, selector);
    count += addRuntimeTests(tests, selector);

    if (!count) {
        printf("No tests matched selector: %s\r\n", (char*)selector);
        printf("Selectors: all, runtime, runtime.bytearray, runtime.hashmap, runtime.ringbuffer, runtime.base58, runtime.stress, stress-memory\r\n");
        return 2;
    }

    return tests.run() ? 0 : 1;
}

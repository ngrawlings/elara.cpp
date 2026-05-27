#include "ElaraReplClientDebugTests.h"

#include <stdlib.h>
#include <sys/time.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/String.h>

using namespace elara;

namespace {
    long long currentMicros() {
        struct timeval tv;
        gettimeofday(&tv, 0);
        return ((long long)tv.tv_sec * 1000000LL) + tv.tv_usec;
    }

    bool validatorBasic() {
        String value("debug-ready");
        return value.trim().length() == 11;
    }

    bool stressStrings(long long duration_us) {
        long long end_time = currentMicros() + duration_us;
        unsigned long long iterations = 0;
        String current;
        while (currentMicros() < end_time) {
            current = String("stress-") + String(iterations);
            if (!current.length()) {
                return UnitTests::fail("stress string build failed");
            }
            iterations++;
        }
        return iterations > 0;
    }

    bool fuzzByteArray(long long duration_us) {
        long long end_time = currentMicros() + duration_us;
        ByteArray bytes;
        unsigned long long iterations = 0;
        srand(1);
        while (currentMicros() < end_time) {
            int op = rand() % 3;
            if (op == 0) {
                bytes.append((char)('a' + (rand() % 26)));
            } else {
                if (op == 1 && bytes.length()) {
                    bytes = bytes.subBytes(0, bytes.length() - 1);
                } else {
                    int original_length = 0;
                    original_length = bytes.length();
                    ByteArray copy = ByteArray(bytes);
                    if (copy.length() != original_length) {
                        return UnitTests::fail("bytearray length mismatch");
                    }
                }
            }
            iterations++;
        }
        return iterations > 0;
    }
}

void registerElaraReplClientDebugTests(UnitTests &tests) {
    tests.addValidatorTest(String("generated.validator.basic"), validatorBasic);
    tests.addStressTest(String("generated.stress.strings"), stressStrings);
    tests.addFuzzTest(String("generated.fuzz.bytearray"), fuzzByteArray);
}

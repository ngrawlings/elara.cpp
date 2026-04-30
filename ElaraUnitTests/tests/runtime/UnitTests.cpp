#include "UnitTests.h"

#include <libelaracore/encoding/Base58.h>
#include <libelaracore/encoding/Base64.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/LinkedList.h>
#include <libelaracore/memory/RingBuffer.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/parsing/CommandLineParser.h>
#include <libelaraio/IndexedDataStore.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelarathreads/memory/InstancePool.h>
#include <libelarathreads/memory/Ref.h>

#include <atomic>
#include <functional>
#include <set>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

namespace elara {

    namespace {

        bool testStringSubstr() {
            String test("01234567890123456789012345678901");
            String sub = test.substr(5, 1);
            if (!sub.equals("5", true))
                return false;

            sub = test.substr(5, 2);
            if (!sub.equals("56", true))
                return false;

            sub = test.substr(5, 0);
            if (!sub.equals("", true))
                return false;

            return true;
        }

        bool testStringReplace() {
            String expected("01234567890123456789012345678901");
            String test("01234567890%REPLACEME%23456789012345678901");

            String res = test.replace("%REPLACEME%", "1");
            if (!res.equals(expected, true)) {
                UnitTests::fail(res);
                return false;
            }

            test = String("01234567890%REPLACEME%789012345678901");
            res = test.replace("%REPLACEME%", "123456");
            if (!res.equals(expected, true)) {
                UnitTests::fail(res);
                return false;
            }

            test = String("01234567890%REP\nLACEME%789012345678901");
            res = test.replace("%REP\nLACEME%", "123456");
            if (!res.equals(expected, true)) {
                UnitTests::fail(res);
                return false;
            }

            return true;
        }

    }

    int addUnitTests(UnitTests &tests, String selector) {
        tests.addTest("String.substr", testStringSubstr);
        tests.addTest("String.replace", testStringReplace);
        return 2;
    }

}
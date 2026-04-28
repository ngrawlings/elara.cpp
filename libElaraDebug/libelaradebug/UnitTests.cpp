//
//  UnitTests.cpp
//  NrDebug
//
//  Created by Nyhl Rawlings on 07/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include "UnitTests.h"

#include <sys/time.h>

namespace elara {

    static long long getCurrentTimeMicros() {
        struct timeval tv;
        gettimeofday(&tv, 0);
        return ((long long)tv.tv_sec * 1000000LL) + tv.tv_usec;
    }

    UnitTests::UnitTests() : run_mode("unit-tests") {
        artifact_builder.setRunMode(run_mode);
    }
    
    void UnitTests::addTest(String name, UNITTEST cb) {
        UNITTEST_ENTRY *test = new UNITTEST_ENTRY;
        
        test->name = name;
        test->method = cb;
        
        Ref<UNITTEST_ENTRY> new_entry = Ref<UNITTEST_ENTRY>(test);
        this->tests.add(new_entry);
    }
    
    void UnitTests::addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count) {
        for(int i=0; i<count; i++)
            this->tests.add(tests.getPtr()[i]);
    }
    
    bool UnitTests::run() {
        bool ret = true;
        int passed = 0;
        int failed = 0;
        long long run_start = getCurrentTimeMicros();

        artifact_builder.setRunMode(run_mode);
        artifact_builder.startRun((int)tests.length());
        
        if (tests.length()) {
            LinkedListState< Ref<UNITTEST_ENTRY> > test_state(&tests);
            Ref<UNITTEST_ENTRY> *obj_out;
            
            while(test_state.iterate(&obj_out)) {
                long long test_start = getCurrentTimeMicros();
                String detail = "ok";
                bool success = false;

                try {
                    if (obj_out->getPtr()->method()) {
                        printf("%s succeeded\r\n", (char*)obj_out->getPtr()->name);
                        success = true;
                        passed++;
                    } else {
                        printf("%s failed\r\n", (char*)obj_out->getPtr()->name);
                        detail = "returned false";
                        ret = false;
                        failed++;
                    }
                } catch (String err) {
                    printf("%s failed with the following error: %s\r\n", (char*)obj_out->getPtr()->name, (char*)err);
                    detail = err;
                    ret = false;
                    failed++;
                } catch (...) {
                    detail = "unknown exception";
                    ret = false;
                    failed++;
                }

                artifact_builder.recordTestResult(
                    obj_out->getPtr()->name,
                    success,
                    detail,
                    getCurrentTimeMicros() - test_start);
            }
        }

        artifact_builder.finishRun(ret, passed, failed, getCurrentTimeMicros() - run_start);
        
        return ret;
    }

    void UnitTests::setRunMode(String mode) {
        run_mode = mode;
        artifact_builder.setRunMode(mode);
    }

    void UnitTests::addRunMetadata(String key, String value) {
        artifact_builder.addMetadata(key, value);
    }

    String UnitTests::getArtifactDirectory() const {
        return artifact_builder.getRunDirectory();
    }
    
    bool UnitTests::fail(const char *msg) {
        throw String(msg);
    }
    
}

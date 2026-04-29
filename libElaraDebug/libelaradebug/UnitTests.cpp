//
//  UnitTests.cpp
//  NrDebug
//
//  Created by Nyhl Rawlings on 07/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include "UnitTests.h"

#include <errno.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

namespace elara {

    static long long getCurrentTimeMicros() {
        struct timeval tv;
        gettimeofday(&tv, 0);
        return ((long long)tv.tv_sec * 1000000LL) + tv.tv_usec;
    }

    UnitTests::UnitTests() : run_mode("unit-tests") {
        artifact_builder.setRunMode(run_mode);
    }

    void UnitTests::addEntry(String name, UNITTEST_KIND kind, UNITTEST validator_method, UNITTEST_TIMED timed_method) {
        UNITTEST_ENTRY *test = new UNITTEST_ENTRY;

        test->name = name;
        test->kind = kind;
        test->validator_method = validator_method;
        test->timed_method = timed_method;

        Ref<UNITTEST_ENTRY> new_entry = Ref<UNITTEST_ENTRY>(test);
        this->tests.add(new_entry);
    }

    void UnitTests::addTest(String name, UNITTEST cb) {
        addValidatorTest(name, cb);
    }

    void UnitTests::addValidatorTest(String name, UNITTEST cb) {
        addEntry(name, UNITTEST_KIND_VALIDATOR, cb, 0);
    }

    void UnitTests::addStressTest(String name, UNITTEST_TIMED cb) {
        addEntry(name, UNITTEST_KIND_STRESS, 0, cb);
    }

    void UnitTests::addFuzzTest(String name, UNITTEST_TIMED cb) {
        addEntry(name, UNITTEST_KIND_FUZZ, 0, cb);
    }
    
    void UnitTests::addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count) {
        for(int i=0; i<count; i++)
            this->tests.add(tests.getPtr()[i]);
    }
    
    bool UnitTests::runKind(UNITTEST_KIND kind, long long duration_us) {
        bool ret = true;
        int passed = 0;
        int failed = 0;
        int selected = 0;
        long long run_start = getCurrentTimeMicros();

        artifact_builder.setRunMode(run_mode);
        artifact_builder.startRun(countTests(kind));
        
        if (tests.length()) {
            LinkedListState< Ref<UNITTEST_ENTRY> > test_state(&tests);
            Ref<UNITTEST_ENTRY> *obj_out;
            
            while(test_state.iterate(&obj_out)) {
                if (obj_out->getPtr()->kind != kind)
                    continue;

                long long test_start = getCurrentTimeMicros();
                String detail = "ok";
                bool success = false;
                selected++;

                try {
                    if (kind == UNITTEST_KIND_VALIDATOR && obj_out->getPtr()->validator_method && obj_out->getPtr()->validator_method()) {
                        printf("%s succeeded\r\n", (char*)obj_out->getPtr()->name);
                        success = true;
                        passed++;
                    } else if (kind != UNITTEST_KIND_VALIDATOR && obj_out->getPtr()->timed_method) {
                        int detail_pipe[2];
                        if (pipe(detail_pipe) != 0) {
                            detail = "pipe failed";
                            ret = false;
                            failed++;
                            artifact_builder.recordTestResult(
                                obj_out->getPtr()->name,
                                success,
                                detail,
                                getCurrentTimeMicros() - test_start);
                            continue;
                        }

                        pid_t pid = fork();
                        int status = 0;

                        if (pid == 0) {
                            bool child_success = false;
                            close(detail_pipe[0]);

                            try {
                                child_success = obj_out->getPtr()->timed_method(duration_us);
                            } catch (String err) {
                                write(detail_pipe[1], (char *)err, err.length());
                                child_success = false;
                            } catch (...) {
                                const char *message = "unknown exception";
                                write(detail_pipe[1], message, 17);
                                child_success = false;
                            }

                            close(detail_pipe[1]);
                            _exit(child_success ? 0 : 1);
                        }

                        close(detail_pipe[1]);

                        if (pid < 0) {
                            close(detail_pipe[0]);
                            detail = "fork failed";
                            ret = false;
                            failed++;
                        } else if (waitpid(pid, &status, 0) < 0) {
                            close(detail_pipe[0]);
                            detail = "waitpid failed";
                            ret = false;
                            failed++;
                        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                            close(detail_pipe[0]);
                            printf("%s succeeded\r\n", (char*)obj_out->getPtr()->name);
                            success = true;
                            passed++;
                        } else if (WIFSIGNALED(status)) {
                            close(detail_pipe[0]);
                            detail = String("terminated by signal %").arg(WTERMSIG(status));
                            printf("%s failed with signal %d\r\n", (char*)obj_out->getPtr()->name, WTERMSIG(status));
                            ret = false;
                            failed++;
                        } else {
                            char detail_buffer[256];
                            ssize_t read_length = read(detail_pipe[0], detail_buffer, sizeof(detail_buffer) - 1);
                            close(detail_pipe[0]);

                            if (read_length > 0) {
                                detail_buffer[read_length] = 0;
                                detail = String(detail_buffer);
                            } else if (read_length < 0 && errno) {
                                detail = String("detail read failed errno=%").arg(errno);
                            } else {
                                detail = "returned false";
                            }

                            printf("%s failed: %s\r\n", (char*)obj_out->getPtr()->name, (char*)detail);
                            ret = false;
                            failed++;
                        }
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

        if (!selected)
            ret = false;

        artifact_builder.finishRun(ret, passed, failed, getCurrentTimeMicros() - run_start);
        
        return ret;
    }

    bool UnitTests::run() {
        return runKind(UNITTEST_KIND_VALIDATOR, 0);
    }

    bool UnitTests::runStress(long long duration_us) {
        return runKind(UNITTEST_KIND_STRESS, duration_us);
    }

    bool UnitTests::runFuzz(long long duration_us) {
        return runKind(UNITTEST_KIND_FUZZ, duration_us);
    }

    int UnitTests::countTests(UNITTEST_KIND kind) const {
        int count = 0;

        if (tests.length()) {
            LinkedListState< Ref<UNITTEST_ENTRY> > test_state(const_cast<LinkedList< Ref<UNITTEST_ENTRY> > *>(&tests));
            Ref<UNITTEST_ENTRY> *obj_out;

            while (test_state.iterate(&obj_out)) {
                if (obj_out->getPtr()->kind == kind)
                    count++;
            }
        }

        return count;
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

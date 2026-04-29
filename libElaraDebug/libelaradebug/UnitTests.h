//
//  UnitTests.hpp
//  NrDebug
//
//  Created by Nyhl Rawlings on 07/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#ifndef UnitTests_hpp
#define UnitTests_hpp

#include <libelaracore/memory/RefArray.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/LinkedList.h>

#include "TestArtifactBuilder.h"

typedef bool (*UNITTEST)();
typedef bool (*UNITTEST_TIMED)(long long duration_us);
typedef enum {
    UNITTEST_KIND_VALIDATOR,
    UNITTEST_KIND_STRESS,
    UNITTEST_KIND_FUZZ
} UNITTEST_KIND;
typedef struct {
    elara::String name;
    UNITTEST_KIND kind;
    UNITTEST validator_method;
    UNITTEST_TIMED timed_method;
} UNITTEST_ENTRY;

namespace elara {
    
    class UnitTests {
    public:
        UnitTests();

        void addTest(String name, UNITTEST cb);
        void addValidatorTest(String name, UNITTEST cb);
        void addStressTest(String name, UNITTEST_TIMED cb);
        void addFuzzTest(String name, UNITTEST_TIMED cb);
        void addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count);
        bool run();
        bool runStress(long long duration_us);
        bool runFuzz(long long duration_us);
        int countTests(UNITTEST_KIND kind) const;
        void setRunMode(String mode);
        void addRunMetadata(String key, String value);
        String getArtifactDirectory() const;
        
        bool static fail(const char *msg);
        
    private:
        LinkedList< Ref<UNITTEST_ENTRY> > tests;
        TestArtifactBuilder artifact_builder;
        String run_mode;

        void addEntry(String name, UNITTEST_KIND kind, UNITTEST validator_method, UNITTEST_TIMED timed_method);
        bool runKind(UNITTEST_KIND kind, long long duration_us);
    };
    
}

#endif /* UnitTests_hpp */

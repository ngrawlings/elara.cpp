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
typedef struct {
    elara::String name;
    UNITTEST method;
} UNITTEST_ENTRY;

namespace elara {
    
    class UnitTests {
    public:
        UnitTests();

        void addTest(String name, UNITTEST cb);
        void addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count);
        bool run();
        void setRunMode(String mode);
        void addRunMetadata(String key, String value);
        String getArtifactDirectory() const;
        
        bool static fail(const char *msg);
        
    private:
        LinkedList< Ref<UNITTEST_ENTRY> > tests;
        TestArtifactBuilder artifact_builder;
        String run_mode;
    };
    
}

#endif /* UnitTests_hpp */

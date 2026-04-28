//
//  UnitTests.hpp
//  libElaraUnitTests
//
//  Created by Nyhl Rawlings on 06/09/2018.
//  Copyright © 2018 Nyhl Rawlings. All rights reserved.
//

#ifndef UnitTests_hpp
#define UnitTests_hpp

#include <libelaracore/memory/RefArray.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/LinkedList.h>

typedef bool (*UNITTEST)();
typedef struct {
    nrcore::String name;
    UNITTEST method;
} UNITTEST_ENTRY;

namespace nrcore {
    
    class UnitTest {
    public:
        
        void addTest(Ref<UNITTEST_ENTRY> test);
        void addTests(RefArray<Ref<UNITTEST_ENTRY>> tests, int count);
        bool run();
        
    private:
        LinkedList<Ref<UNITTEST_ENTRY>> tests;
    };
    
}

#endif /* UnitTests_hpp */

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

typedef bool (*UNITTEST)();
typedef struct {
    elara::String name;
    UNITTEST method;
} UNITTEST_ENTRY;

namespace elara {
    
    class UnitTests {
    public:
        
        void addTest(String name, UNITTEST cb);
        void addTests(RefArray< Ref<UNITTEST_ENTRY> > tests, int count);
        bool run();
        
        bool static fail(const char *msg);
        
    private:
        LinkedList< Ref<UNITTEST_ENTRY> > tests;
    };
    
}

#endif /* UnitTests_hpp */

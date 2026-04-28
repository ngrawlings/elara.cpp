//
//  UnitTests.cpp
//  NrDebug
//
//  Created by Nyhl Rawlings on 07/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include "UnitTests.h"

namespace nrcore {
    
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
        
        if (tests.length()) {
            LinkedListState< Ref<UNITTEST_ENTRY> > test_state(&tests);
            Ref<UNITTEST_ENTRY> *obj_out;
            
            while(test_state.iterate(&obj_out)) {
                try {
                    if (obj_out->getPtr()->method()) {
                        printf("%s succeeded\r\n", (char*)obj_out->getPtr()->name);
                    } else {
                        printf("%s failed\r\n", (char*)obj_out->getPtr()->name);
                        ret = false;
                    }
                } catch (String err) {
                    printf("%s failed with the following error: %s\r\n", (char*)obj_out->getPtr()->name, (char*)err);
                    ret = false;
                }
            }
        }
        
        return ret;
    }
    
    bool UnitTests::fail(const char *msg) {
        throw String(msg);
    }
    
}

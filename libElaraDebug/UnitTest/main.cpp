//
//  main.cpp
//  UnitTest
//
//  Created by Nyhl Rawlings on 07/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include <iostream>
#include <libelaradebug/UnitTests.h>

using namespace elara;

bool testUnitTestingSuccess() {
    return true;
}

bool testUnitTestingFailed() {
    return false;
}

bool testUnitTestingException() {
    UnitTests::fail("Faield by calling fail");
    return true;
}

int main(int argc, const char * argv[]) {
    UnitTests tests;
    
    tests.addTest("testUnitTestingSuccess", testUnitTestingSuccess);
    tests.addTest("testUnitTestingFailed", testUnitTestingFailed);
    tests.addTest("testUnitTestingException", testUnitTestingException);
    tests.run();
    
    return 0;
}

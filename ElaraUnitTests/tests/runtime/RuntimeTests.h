#ifndef ElaraUnitTests_RuntimeTests_h
#define ElaraUnitTests_RuntimeTests_h

#include <libelaracore/memory/String.h>
#include <libelaradebug/UnitTests.h>

namespace elara {

    int addRuntimeTests(UnitTests &tests, String selector);
    void addRuntimeMetadata(UnitTests &tests, String selector);

}

#endif

//
//  Base64.h
//  libelaracore
//
//  Created by Nyhl Rawlings on 29/04/2026.
//

#ifndef Base64_h
#define Base64_h

#include <stdio.h>
#include "../memory/Memory.h"

namespace elara {

    class Base64 {
    public:
        static Memory encode(Memory mem);
        static Memory decode(Memory mem);
    };

}

namespace elara {
namespace core {
namespace encoding {
    using ::elara::Base64;
}
}
}

#endif /* Base64_h */

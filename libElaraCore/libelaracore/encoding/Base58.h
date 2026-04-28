//
//  Base58.hpp
//  libelaracore
//
//  Created by Nyhl Rawlings on 17/10/2019.
//  Copyright © 2019 N G Rawlings. All rights reserved.
//

#ifndef Base58_hpp
#define Base58_hpp

#include <stdio.h>
#include "../memory/Memory.h"

namespace elara {

    class Base58 {
    public:
        static Memory encode(Memory mem);
        static Memory decode(Memory mem);
    };

}

#endif /* Base58_hpp */

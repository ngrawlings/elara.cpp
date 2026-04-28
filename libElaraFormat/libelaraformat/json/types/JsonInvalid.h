//
//  JsonInvalid.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef JsonInvalid_hpp
#define JsonInvalid_hpp

#include <stdio.h>
#include "JsonValue.h"

namespace elara {
    
    class JsonInvalid : public JsonValue {
    public:
        JsonInvalid(String json);
        JsonInvalid(const JsonInvalid &json);
        virtual ~JsonInvalid();
        
        TYPE getType() const;
        String toString() const;
        
    protected:
        String json;
    };
    
}

#endif /* JsonInvalid_hpp */

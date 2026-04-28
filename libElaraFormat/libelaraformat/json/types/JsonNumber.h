//
//  JsonNumber.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef JsonNumber_hpp
#define JsonNumber_hpp

#include <stdio.h>
#include "JsonValue.h"

namespace nrcore {
    
    class JsonNumber : public JsonValue {
    public:
        JsonNumber(String json);
        JsonNumber(int value);
        JsonNumber(const JsonNumber &json);
        virtual ~JsonNumber();
        
        TYPE getType() const;
        String toString() const;
        
    protected:
        String value;
    };
    
}

#endif /* JsonNumber_hpp */

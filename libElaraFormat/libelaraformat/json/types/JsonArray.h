//
//  JsonArray.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef JsonArray_hpp
#define JsonArray_hpp

#include <stdio.h>
#include "JsonValue.h"
#include <libelaracore/memory/Array.h>

namespace nrcore {
    
    class JsonArray : public JsonValue {
    public:
        JsonArray(String json);
        JsonArray(const JsonArray &json);
        virtual ~JsonArray();
        
        TYPE getType() const;
        String toString() const;
        
        bool parse(String json);
        
        Array< Ref<JsonValue> > getArray();
        
        void addValue(Ref<JsonValue> value);
        
    protected:
        Array< Ref<JsonValue> > values;
    };
    
}

#endif /* JsonArray_hpp */

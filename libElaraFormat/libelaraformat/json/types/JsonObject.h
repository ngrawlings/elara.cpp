//
//  Json.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 17/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef JsonObject_hpp
#define JsonObject_hpp

#include <stdio.h>

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/HashMap.h>

#include "JsonValue.h"

namespace elara {

    class JsonObject : public JsonValue {
        
    public:
        JsonObject(String JsonObject);
        JsonObject(const JsonObject &json);
        virtual ~JsonObject();
        
        TYPE getType() const;
        String toString() const;
        
        bool parse(String json);
        
        Ref<JsonValue> getValue(String name);
        
        void addValue(String name, Ref<JsonValue> value);
        
        HashMap< JsonValue > getValues();
        
    protected:
        TYPE type;
        HashMap< JsonValue > values;
        String value;
    };
    
}

#endif /* JsonObject_hpp */

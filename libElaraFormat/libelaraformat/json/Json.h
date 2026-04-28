//
//  Json.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Json_hpp
#define Json_hpp

#include <stdio.h>
#include "types/JsonValue.h"
#include "types/JsonArray.h"
#include "types/JsonObject.h"
#include "types/JsonString.h"
#include "types/JsonNumber.h"
#include "types/JsonInvalid.h"

#include <libelaracore/memory/String.h>

namespace nrcore {
    
    class Json {
    public:
        Json(String json);
        Json(JsonValue::TYPE type);
        Json(Ref<JsonValue> jval);
        Json(const Json &json);
        virtual ~Json();
        
        JsonValue::TYPE getType() const;
        Ref<JsonValue> getValue() const;
        
        Ref<JsonValue> getJsonValue(String path) const;
        String getStringValue(String path) const;
        int getIntValue(String path) const;
        Array< Ref<JsonValue> > getArray(String path) const;
        
        bool setValue(String path, Ref<JsonValue> value);
        
        String toString();
        
    protected:
        Ref<JsonValue> value;
    };
    
}

#endif /* Json_hpp */

//
//  JsonValue.cpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "JsonValue.h"

#include "JsonArray.h"
#include "JsonObject.h"
#include "JsonNumber.h"
#include "JsonString.h"
#include "JsonInvalid.h"

namespace nrcore {
    
    JsonValue::JsonValue() {
    }
    
    JsonValue::~JsonValue() {
    }
    
    JsonValue::TYPE JsonValue::getType() const {
        return INVALID;
    }
    
    String JsonValue::toString() const {
        return "";
    }
    
    JsonValue::TYPE JsonValue::getJsonType(String json) {
        json = json.trim();
        switch (json[0]) {
            case '{':
                return OBJECT;
       
            case '\"':
                return STRING;

            case '[':
                return ARRAY;
        }
        return VALUE;
    }
    
    Ref<JsonValue> JsonValue::getJsonValue(String json) {
        TYPE type = getJsonType(json);
        switch(type) {
            case OBJECT:
                return Ref<JsonValue>(new JsonObject(json));
            case STRING:
                return Ref<JsonValue>(new JsonString(json));
            case ARRAY:
                return Ref<JsonValue>(new JsonArray(json));
            case VALUE:
                return Ref<JsonValue>(new JsonNumber(json));
            default:
                return Ref<JsonValue>(new JsonInvalid(json));
        }
    }
    
}

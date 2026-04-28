//
//  JsonNumber.cpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "JsonNumber.h"

namespace elara {
    
    JsonNumber::JsonNumber(String json) {
        this->value = json;
    }
    
    JsonNumber::JsonNumber(int value) {
        this->value = String("%").arg(value);
    }
    
    JsonNumber::JsonNumber(const JsonNumber &json) {
        this->value = json.value;
    }
    
    JsonNumber::~JsonNumber() {
        
    }
    
    JsonNumber::TYPE JsonNumber::getType() const {
        return VALUE;
    }
    
    String JsonNumber::toString() const {
        return value;
    }
    
}

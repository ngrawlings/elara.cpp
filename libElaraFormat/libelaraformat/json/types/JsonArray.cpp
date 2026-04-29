//
//  JsonArray.cpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "JsonArray.h"
#include <libelaracore/memory/LinkedList.h>

namespace elara {
    
    JsonArray::JsonArray(String json) {
        parse(json);
    }
    
    JsonArray::JsonArray(const JsonArray &json) {
        this->values = json.values;
    }
    
    JsonArray::~JsonArray() {
        
    }
    
    JsonArray::TYPE JsonArray::getType() const {
        return ARRAY;
    }
    
    String JsonArray::toString() const {
        String ret = "";
        ssize_t arr_len = values.length();
        if (arr_len) {
            ret = values.get(0).getPtr()->toString();
            for (int i=1; i<arr_len; i++) {
                ret += String(",%").arg(values.get(i).getPtr()->toString());
            }
        }
        return String("[%]").arg(ret);
    }
    
    bool JsonArray::parse(String json) {
        LinkedList<char> symbols;
        bool in_array = false;
        bool quotation = false;
        
        String val;
        
        ssize_t slen = json.length();
        for (int i=0; i<slen; i++) {
            switch (json[i]) {
                case '\\':
                    if (quotation || in_array) {
                        val += json[i];
                        if (i + 1 < slen)
                            val += json[i + 1];
                    }
                    i++;
                    break;
                    
                case '\"':
                    if (quotation && symbols.length()) {
                        if (symbols.get(symbols.lastNode()) == '\"') {
                            if (symbols.get(symbols.prevNode(symbols.lastNode())) != '\"')
                                quotation = false;
                            
                            val += json[i];
                            symbols.removeNode(symbols.lastNode());
                        } else {
                            val += json[i];
                            if (symbols.get(symbols.lastNode()) == '\"')
                                symbols.removeNode(symbols.lastNode());
                            else
                                symbols.add('\"');
                        }
                    } else {
                        quotation = true;
                        val += json[i];
                        
                        if (symbols.get(symbols.lastNode()) != '\"')
                            symbols.add('\"');
                        else
                            symbols.removeNode(symbols.lastNode());
                    }
                    break;
                    
                case '[':
                    symbols.add(json[i]);
                    in_array = true;
                    if (symbols.length()>1)
                        val += json[i];
                    break;
                    
                case '{':
                    symbols.add(json[i]);
                    in_array = true;
                    if (symbols.length()>1)
                        val += json[i];
                    break;
                    
                default:
                {
                    if (in_array) {
                        if (symbols.length() == 1 && (json[i] == ',' || json[i] == ']')) {
                            if (val.length()) {
                                values.push(JsonValue::getJsonValue(val));
                                val = "";
                            }
                            if (json[i] == ']')
                                in_array = false;
                        } else if (json[i] == ']') {
                            val += json[i];
                            symbols.removeNode(symbols.lastNode());
                            if (!symbols.length())
                                in_array = false;
                        } else if (json[i] == '}') {
                            val += json[i];
                            symbols.removeNode(symbols.lastNode());
                        } else {
                            val += json[i];
                        }
                    }
                }
                    
            }
        }
        
        return true;
    }
    
    Array< Ref<JsonValue> > JsonArray::getArray() {
        return values;
    }
    
    void JsonArray::addValue(Ref<JsonValue> value) {
        values.push(value);
    }
    
}

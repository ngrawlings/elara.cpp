//
//  JsonString.cpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 19/09/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "JsonString.h"

namespace elara {
    
    JsonString::JsonString(String json) {
        this->value = decode(json);
    }

    JsonString::JsonString(String value, bool raw_value) {
        (void)raw_value;
        this->value = value;
    }
    
    JsonString::JsonString(const JsonString &json) {
        this->value = json.value;
    }
    
    JsonString::~JsonString() {
        
    }
    
    JsonString::TYPE JsonString::getType() const {
        return STRING;
    }
    
    String JsonString::toString() const {
        return String("\"%\"").arg(encode(value));
    }
    
    String JsonString::getValue() {
        return value;
    }

    String JsonString::decode(String json) {
        String trimmed = json.trim();
        String decoded;
        int start = 0;
        int end = (int)trimmed.length();

        if (trimmed.length() >= 2 && trimmed[0] == '"' && trimmed[trimmed.length() - 1] == '"') {
            start = 1;
            end--;
        }

        for (int i=start; i<end; i++) {
            char ch = trimmed.byteAt(i);
            if (ch == '\\' && i + 1 < end) {
                i++;
                switch (trimmed.byteAt(i)) {
                case '"':
                    decoded += String('"');
                    break;
                case '\\':
                    decoded += String('\\');
                    break;
                case '/':
                    decoded += String('/');
                    break;
                case 'b':
                    decoded += String('\b');
                    break;
                case 'f':
                    decoded += String('\f');
                    break;
                case 'n':
                    decoded += String('\n');
                    break;
                case 'r':
                    decoded += String('\r');
                    break;
                case 't':
                    decoded += String('\t');
                    break;
                default:
                    decoded += String(trimmed.byteAt(i));
                    break;
                }
            } else {
                decoded += String(ch);
            }
        }

        return decoded;
    }

    String JsonString::encode(String value) {
        String encoded;

        for (int i=0; i<value.length(); i++) {
            char ch = value.byteAt(i);
            switch (ch) {
            case '\\':
                encoded += "\\\\";
                break;
            case '"':
                encoded += "\\\"";
                break;
            case '\b':
                encoded += "\\b";
                break;
            case '\f':
                encoded += "\\f";
                break;
            case '\n':
                encoded += "\\n";
                break;
            case '\r':
                encoded += "\\r";
                break;
            case '\t':
                encoded += "\\t";
                break;
            default:
                encoded += String(ch);
                break;
            }
        }

        return encoded;
    }
    
}

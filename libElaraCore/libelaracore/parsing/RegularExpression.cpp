//
//  RegularExpression.cpp
//  libElaraCore
//
//  Created by Nyhl Rawlings on 04/03/14.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#include "RegularExpression.h"

namespace elara {

    RegularExpression::RegularExpression(String expr) {
        this->expr = expr;
        if (regcomp(&regex, this->expr, REG_EXTENDED))
            throw "Failed to Compile";
    }

    RegularExpression::RegularExpression(const RegularExpression &regex) {
        this->expr = regex.expr;
        if (regcomp(&this->regex, this->expr, REG_EXTENDED))
            throw "Failed to Compile";
    }

    RegularExpression::~RegularExpression() {
        regfree(&regex);
    }

    String &RegularExpression::getExpression() {
        return expr;
    }

    bool RegularExpression::match(String str) {
        return !regexec(&regex, str, 0, NULL, 0);
    }

    StringList RegularExpression::extract(String str, int max_matches) {
        StringList ret;

        if(max_matches <= 0)
            return ret;

        regmatch_t matches[16];

        if(max_matches > 16)
            max_matches = 16;

        if(regexec(&regex, str, max_matches, matches, 0))
            return ret;

        for(int i = 0; i < max_matches; i++) {
            if(matches[i].rm_so < 0 || matches[i].rm_eo < 0)
                break;

            ret.append(str.substr(
                matches[i].rm_so,
                matches[i].rm_eo - matches[i].rm_so
            ));
        }

        return ret;
    }

}

//
//  RegularExpression.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 04/03/14.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__RegularExpression__
#define __libElaraCore__RegularExpression__

#include <regex.h>

#include <libelaracore/memory/String.h>

namespace elara {

    class RegularExpression {
    public:
        RegularExpression(String expr);
        RegularExpression(const RegularExpression &regex);
        virtual ~RegularExpression();

        String &getExpression();

        bool match(String str);

    protected:
        String expr;
        regex_t regex;
    };

};

#endif /* defined(__libElaraCore__RegularExpression__) */

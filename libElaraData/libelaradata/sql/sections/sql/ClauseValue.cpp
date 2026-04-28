//
//  ClauseValue.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "ClauseValue.h"

namespace elara {
    namespace sql {

        ClauseValue::ClauseValue(Builder *gb, String f1, String comparitor, String f2) :
            field1(Value(gb, FIELD, f1)), comparitor(comparitor), field2(Value(gb, STRING, f2)) {
                _operator = String("AND");
        }

        ClauseValue::ClauseValue(Value f1, String comparitor, Value f2) :
            field1(f1), comparitor(comparitor), field2(f2) {
                _operator = String("AND");
        }

        ClauseValue::ClauseValue(const ClauseValue &clause) :
            field1(clause.field1), comparitor(clause.comparitor), field2(clause.field2) {
                _operator = clause._operator;
        }

        ClauseValue::~ClauseValue() {
            
        }

        String ClauseValue::toString() {
            return field1.toString() + comparitor + field2.toString();
        }

    }
}

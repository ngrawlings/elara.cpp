//
//  ClauseValue.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef ClauseValue_hpp
#define ClauseValue_hpp

#include <stdio.h>
#include "Clause.h"

namespace nrcore {
    
    class Builder;
    
    namespace sql {
    
        class ClauseValue : public Clause {
        public:
            ClauseValue(Builder *gb, String f1, String comparitor, String f2);
            ClauseValue(Value f1, String comparitor, Value f2);
            ClauseValue(const ClauseValue &clause);
            virtual ~ClauseValue();
            
            String toString();
            
        protected:
            String _operator;
            Value field1, field2;
            String comparitor;
        };
        
    }
    
}

#endif /* ClauseValue_hpp */

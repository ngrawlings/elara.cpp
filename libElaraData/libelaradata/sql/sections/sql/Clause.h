//
//  Clause.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Clause_hpp
#define Clause_hpp

#include <stdio.h>
#include <libelaracore/memory/String.h>

namespace nrcore {
    class Builder;
    
    namespace sql {
    
        class Clause {
        public:
            typedef enum {
                STRING,
                FIELD,
                RAW
            } TYPE;
            
            class Value {
            public:
                Value(Builder *gb, TYPE type, String value);
                Value(const Value &value);
                virtual ~Value();
                
                TYPE type();
                String value();
                
                String toString();
                
            protected:
                TYPE _type;
                String _value;
                
            };
            
        public:
            Clause(String _operator="AND");
            Clause(const Clause& clause);
            virtual ~Clause();
            
            String getOperator();
            
            virtual String toString() = 0;
            
        protected:
            String _operator;
        };
    
    }
}

#endif /* Clause_hpp */

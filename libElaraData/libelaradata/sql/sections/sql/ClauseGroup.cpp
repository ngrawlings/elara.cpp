//
//  ClauseGroup.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "ClauseGroup.h"

namespace nrcore {
    namespace sql {
        
        ClauseGroup::ClauseGroup() {
            
        }
        
        ClauseGroup::ClauseGroup(const ClauseGroup &clause) {
            clauses = clause.clauses;
        }
        
        ClauseGroup::~ClauseGroup() {
            
        }
        
        void ClauseGroup::addClause(Ref<Clause> clause) {
            clauses.push(clause);
        }
        
        String ClauseGroup::toString() {
            String ret;
            
            ssize_t len = clauses.length();
            if (len) {
                ret = clauses[0].getPtr()->toString();
                for (int i=1; i<len; i++)
                    ret += String(" % %").arg(clauses[i].getPtr()->getOperator()).arg(clauses[i].getPtr()->toString());
            }
            
            return String("(%)").arg(ret);
        }
    
    }
}

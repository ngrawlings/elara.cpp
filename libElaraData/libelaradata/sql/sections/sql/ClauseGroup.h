//
//  ClauseGroup.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef ClauseGroup_hpp
#define ClauseGroup_hpp

#include <stdio.h>
#include "Clause.h"
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

namespace nrcore {
    namespace sql {
    
        class ClauseGroup : public Clause {
        public:
            ClauseGroup();
            ClauseGroup(const ClauseGroup &clause);
            virtual ~ClauseGroup();
            
            void addClause(Ref<Clause> clause);
            
            String toString();
            
        protected:
            Array<Ref<Clause>> clauses;
        };
    
    }
}

#endif /* ClauseGroup_hpp */

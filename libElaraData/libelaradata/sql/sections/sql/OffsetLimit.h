//
//  OffsetLimit.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 03/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef OffsetLimit_hpp
#define OffsetLimit_hpp

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

namespace nrcore {
    namespace sql {
        
        class OffsetLimit {
        public:
            OffsetLimit();
            OffsetLimit(int limit);
            OffsetLimit(long long offset, int limit);
            OffsetLimit(const OffsetLimit &ol);
            virtual ~OffsetLimit();
            
            void offset(long long offset);
            void limit(int limit);
            
            String toString();
            
            
        protected:
            long long _offset;
            int _limit;
        };
    
    }
}

#endif /* OffsetLimit_hpp */

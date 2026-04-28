//
//  OffsetLimit.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 03/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "OffsetLimit.h"

namespace nrcore {
    namespace sql {
        
        OffsetLimit::OffsetLimit() {
            _offset = -1;
            _limit = -1;
        }
        
        OffsetLimit::OffsetLimit(int limit) {
            _offset = -1;
            _limit = limit;
        }
     
        OffsetLimit::OffsetLimit(long long offset, int limit) {
            _offset = offset;
            _limit = limit;
        }
        
        OffsetLimit::OffsetLimit(const OffsetLimit &ol) {
            _offset = ol._offset;
            _limit = ol._limit;
        }
        
        OffsetLimit::~OffsetLimit() {
            
        }
        
        void OffsetLimit::offset(long long offset) {
            _offset = offset;
        }
        
        void OffsetLimit::limit(int limit) {
            _limit = limit;
        }
        
        String OffsetLimit::toString() {
            if (_offset == -1 && _limit == -1)
                return String("");
            
            if (_offset == -1)
                return String("LIMIT %").arg(_limit);
            
            return String("LIMIT %, %").arg(_offset).arg(_limit);
        }
    
    }
}

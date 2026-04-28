//
//  Fields.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 17/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Fields.h"

namespace elara {
    namespace sql {
        
        Fields::Fields() {
            
        }
        
        Fields::Fields(const Fields& fields) : fields(fields.fields) {
            
        }
        
        Fields::~Fields() {
            
        }
        
        void Fields::clear() {
            fields.clear();
        }
        
        void Fields::addField(String field) {
            fields.push(field);
        }
        
        String Fields::toString() {
            String ret;
            
            size_t len = fields.length();
            if (len) {
                ret = String("`%`").arg(fields[0]);
                for (int i=1; i<len; i++)
                ret += String(", `%`").arg(fields[i]);
            }
            
            return ret;
        }
        
    }
}

//
//  Fields.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 17/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Fields_hpp
#define Fields_hpp

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

namespace nrcore {
    namespace sql {
        
        class Fields {
            public:
            Fields();
            Fields(const Fields& fields);
            virtual ~Fields();
            
            void clear();
            
            void addField(String field);
            
            virtual String toString();
            
            protected:
            Array<String> fields;
        };
        
    }
}

#endif /* Fields_hpp */

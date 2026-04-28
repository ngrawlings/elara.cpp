//
//  FieldDescriptor.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 04/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef FieldDescriptor_Base_hpp
#define FieldDescriptor_Base_hpp

#include <stdio.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

namespace elara {
    namespace sql {
        
        class FieldDescriptor {
        public:
            FieldDescriptor();
            virtual ~FieldDescriptor();
            
            virtual FieldDescriptor& setParameter(String param) { return *this; }
            virtual FieldDescriptor& setUnsigned(bool val) { return *this; }
            virtual FieldDescriptor& setZeroFill(bool val) { return *this; }
            virtual FieldDescriptor& setBinary(bool val) { return *this; }
            virtual FieldDescriptor& notNull(bool val) { return *this; }
            virtual FieldDescriptor& setDefault(String _default) { return *this; }
            
            virtual FieldDescriptor& setExtra(String extra) { return *this; }
            
            virtual FieldDescriptor& setAutoIncrement() = 0;
            
            virtual FieldDescriptor& setEncoding(String encoding) { return *this; }
            virtual FieldDescriptor& setCollation(String collation) { return *this; }
            virtual FieldDescriptor& setComment(String comment) { return *this; }
            
            virtual FieldDescriptor& setIndex(String type) { return *this; }
            
            virtual String toString() = 0;
            
        };
        
    }
}

#endif /* FieldDescriptor_Base_hpp */

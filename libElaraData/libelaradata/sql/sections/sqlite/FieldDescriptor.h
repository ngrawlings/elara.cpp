//
//  FieldDescriptor.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 04/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef FieldDescriptor_hpp
#define FieldDescriptor_hpp

#include <stdio.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

#include "../sql/FieldDescriptor.h"

namespace nrcore {
    
    namespace sqlite {
        
        class FieldDescriptor : public sql::FieldDescriptor {
        public:
            typedef enum {
                PRIMARY,
                INDEX,
                UNIQUE,
                FULLTEXT
            } INDEX_TYPE;
            
            typedef struct {
                String name;
                INDEX_TYPE type;
            } _INDEX;
            
            typedef enum {
                TINYINT             = 0,
                SMALLINT            = 1,
                MEDIUMINT           = 2,
                INT                 = 4,
                BIGINT              = 5,
                FLOAT               = 6,
                DOUBLE              = 7,
                DOUBLE_PRECISION    = 8,
                REAL                = 9,
                DECIMAL             = 10,
                BIT                 = 11,
                SERIAL              = 12

            } TYPE;
            
        public:
            FieldDescriptor(String name, TYPE type);
            FieldDescriptor(const FieldDescriptor& fd);
            virtual ~FieldDescriptor();
            
            FieldDescriptor& setParameter(String param);
            FieldDescriptor& setUnsigned(bool val);
            FieldDescriptor& setZeroFill(bool val);
            FieldDescriptor& setBinary(bool val);
            FieldDescriptor& notNull(bool val);
            FieldDescriptor& setDefault(String _default);
            
            FieldDescriptor& setExtra(String extra);
            
            FieldDescriptor& setAutoIncrement();
            FieldDescriptor& setUpdateCurrentDate();
            FieldDescriptor& setSerialDefaultValue();
            
            FieldDescriptor& setEncoding(String encoding);
            FieldDescriptor& setCollation(String collation);
            FieldDescriptor& setComment(String comment);
            
            FieldDescriptor& setIndex(INDEX_TYPE type);
            
            static String getTypeString(TYPE type);
            static TYPE getTypeByString(String type);
            
            virtual String toString();
            String getIndex();
            
        protected:
            String name;
            TYPE type;
            String parameter; // tipically the length displaied within the cli client, however it can be the enum values
            bool _unsigned;
            bool zerofill;
            bool binary;
            bool notnull;
            String _default;
            String extra;
            String encoding;
            String collation;
            String comment;
            
            Ref<_INDEX> index;
            
            static const char* field_types[];
        };
        
    }
    
}

#endif /* FieldDescriptor_hpp */

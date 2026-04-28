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
    
    namespace mysql {
        
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
                INT                 = 3,
                BIGINT              = 4,
                FLOAT               = 5,
                DOUBLE              = 6,
                DOUBLE_PRECISION    = 7,
                REAL                = 8,
                DECIMAL             = 9,
                BIT                 = 10,
                SERIAL              = 11,
                BOOL                = 12,
                BOOLEAN             = 13,
                DEC                 = 14,
                FIXED               = 15,
                NUMERIC             = 16,
                CHAR                = 17,
                VARCHAR             = 18,
                TINYTEXT            = 19,
                TEXT                = 20,
                MEDIUMTEXT          = 21,
                LONGTEXT            = 22,
                TINYBLOB            = 23,
                MEDIUMBLOB          = 24,
                BLOB                = 25,
                LONGBLOB            = 26,
                BINARY              = 27,
                VARBINARY           = 28,
                JSON                = 29,
                ENUM                = 30,
                SET                 = 31,
                DATE                = 32,
                DATETIME            = 33,
                TIMESTAMP           = 34,
                TIME                = 35,
                YEAR                = 36,
                GEOMETRY            = 37,
                POINT               = 38,
                LINESTRING          = 39,
                POLYGON             = 40,
                MULTIPOINT          = 41,
                MULTILINESTRING     = 42,
                MULTIPOLYGON        = 43,
                GEOMETRYCOLLECTION  = 44
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
            
            FieldDescriptor& setIndex(String type);
            
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

//
//  AlterTableField.hpp
//  Tests
//
//  Created by Nyhl Rawlings on 18/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef AlterTableField_hpp
#define AlterTableField_hpp

#include <libelaradata/sql/sections/mysql/FieldDescriptor.h>

#include <libelaracore/memory/String.h>

namespace nrcore {
    
    using namespace mysql;
    
    namespace sqlite {
        
        class AlterTableField : public FieldDescriptor {
            public:
            typedef enum {
                ADD,
                DROP,
                CHANGE
            } ACTION;
            public:
            AlterTableField(String name, FieldDescriptor::TYPE type);
            AlterTableField(const AlterTableField &atf);
            AlterTableField(FieldDescriptor *fd);
            virtual ~AlterTableField();
            
            AlterTableField& add();
            AlterTableField& drop();
            AlterTableField& change(String new_name);
            AlterTableField& afterColumn(String field);
            
            String toString();
            
            protected:
            ACTION action;
            String new_name;
            String after_column;
        };
        
    }
}

#endif /* AlterTableField_hpp */

//
//  MysqlGenerator.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef MysqlGenerator_hpp
#define MysqlGenerator_hpp

#include <stdio.h>
#include <mysql/mysql.h>

#include "Builder.h"

namespace elara {
    
    class MysqlBuilder : public Builder {
    public:
        MysqlBuilder(MYSQL *mysql, String name);
        MysqlBuilder(const MysqlBuilder& gen);
        virtual ~MysqlBuilder();
        
        String escape(String str);
        String sql(TYPE type);
        
        Ref<FieldDescriptor> getFieldDescriptor(String name, String type);
        
    protected:
        String _select();
        String insert();
        String update();
        String _delete();
        String create();
        String alter();
        String drop();
        String truncate();

    private:
        MYSQL *mysql;
    };
    
}

#endif /* MysqlGenerator_hpp */

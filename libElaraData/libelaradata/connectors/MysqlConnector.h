//
//  MysqlConnector.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 07/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef MysqlConnector_hpp
#define MysqlConnector_hpp

#include <stdio.h>
#include "Connector.h"
#include <libelaradata/sql/builders/MysqlBuilder.h>

#include <mysql/mysql.h>

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

#include "../result/ResultSet.h"

namespace elara {
    
    class MysqlConnector : public Connector {
    public:
        MysqlConnector(const char* host, int port, const char* username, const char* password, const char* database=0);
        virtual ~MysqlConnector();
        
        Ref<Builder> getBuilder(String table);
        
        void createDatabase(String name);
        void dropDatabase(String name);
        bool tableExists(String table);
        
        void execute(String sql);
        ResultSet query(String sql);
        
        unsigned int lastInsertId();
        
    protected:
        MYSQL *con;
        
    };
    
}

#endif /* MysqlConnector_hpp */

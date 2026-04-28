//
//  SqliteConnector.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 17/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef SqliteConnector_hpp
#define SqliteConnector_hpp

#include "Connector.h"
#include <sqlite3.h>

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

#include "../result/ResultSet.h"

namespace elara {
    
    class SqliteConnector : public Connector {
    public:
        SqliteConnector(String path);
        virtual ~SqliteConnector();
        
        Ref<Builder> getBuilder(String table);
        
        void createDatabase(String name);
        void dropDatabase(String name);
        bool tableExists(String table);
        
        void execute(String sql);
        ResultSet query(String sql);
        
        unsigned int lastInsertId();

    protected:
        sqlite3 *db;
    };
    
}

#endif /* SqliteConnector_hpp */

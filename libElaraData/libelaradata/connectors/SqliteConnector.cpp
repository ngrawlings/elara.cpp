//
//  SqliteConnector.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 17/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "SqliteConnector.h"

#include <libelaracore/exception/Exception.h>
#include <libelaradata/sql/builders/SqliteBuilder.h>
#include <libelaradata/sql/sections/sqlite/FieldDescriptor.h>

namespace elara {

    SqliteConnector::SqliteConnector(String path) {
        if (sqlite3_open(path, &db) != SQLITE_OK) {
            int errno = sqlite3_errcode(db);
            String errmsg = sqlite3_errmsg(db);
            sqlite3_close(db);
            
            throw Exception(errno, errmsg);
        }
    }

    SqliteConnector::~SqliteConnector() {
        sqlite3_close(db);
    }
    
    Ref<Builder> SqliteConnector::getBuilder(String table) {
        return Ref<Builder>(new SqliteBuilder(table));
    }
    
    void SqliteConnector::createDatabase(String name) {
        throw Exception(-1, "Sqlite databases are file based");
    }
    
    void SqliteConnector::dropDatabase(String name) {
        throw Exception(-1, "Sqlite databases are file based");
    }
    
    bool SqliteConnector::tableExists(String table) {
        return false;
    }
    
    void SqliteConnector::execute(String sql) {
        
    }
    
    ResultSet SqliteConnector::query(String sql) {
        return ResultSet(this, 0, 0);
    }

    unsigned int SqliteConnector::lastInsertId() {
        return 0;
    }

}

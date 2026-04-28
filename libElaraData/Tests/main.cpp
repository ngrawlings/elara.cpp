//
//  main.cpp
//  Tests
//
//  Created by Nyhl Rawlings on 14/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include <iostream>
#include <libelaracore/exception/Exception.h>
#include <libelaradata/connectors/MysqlConnector.h>
#include <libelaradata/connectors/SqliteConnector.h>
#include "TestModel.h"


using namespace nrcore;

int main(int argc, const char * argv[]) {

    try {
        //MysqlConnector con("127.0.0.1", 3306, "root", "MovingOn", "test");
        SqliteConnector con("./test.sqlite");
        TestModel tm(&con);
        
        if (!con.tableExists("test_table"))
            printf("table does not exist\n");
        
        if (!tm.emailExists("test@test.com"))
            tm.insert("test@test.com", "A simple test");
        
        Array<String> emails = tm.list();
        for (int i=0; i<emails.length(); i++)
            printf("%s\n", emails[i].operator char *());
        
    } catch (Exception e) {
        printf("%d: %s\n", e.getErrorCode(), e.getMessage());
    }
    
    printf("Finished\n");
    
    return 0;
}

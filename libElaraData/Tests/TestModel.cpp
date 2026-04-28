//
//  TestModel.cpp
//  Tests
//
//  Created by Nyhl Rawlings on 15/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "TestModel.h"
#include <libelaradata/sql/sections/sql/ClauseValue.h>
#include <libelaradata/sql/sections/mysql/AlterTableField.h>

TestModel::TestModel(Connector* con) : Model(con, "test_table") {
    runMigration();
}

TestModel::~TestModel() {
    
}

bool TestModel::emailExists(String email) {
    Builder* b = getBuilder()->clear();
    
    b->select("id")->setClause(Ref<Clause>(new ClauseValue(b, "email", "=", email)));
    ResultSet res = con->query(b->sql(Builder::SELECT));

    return res.length()>0;
}

void TestModel::insert(String email, String description) {
    Builder* b = getBuilder()->clear();
    
    b->value("email", email)->value("description", description);
    con->execute(b->sql(Builder::INSERT));
}

Array<String> TestModel::list() {
    Builder* b = getBuilder()->clear();
    
    b->select("id")->select("email");
    ResultSet res = con->query(b->sql(Builder::SELECT));
    
    Array<String> ret;
    
    Row *row;
    while ((row = res.next()))
        ret.push(row->getString(1));
    
    return ret;
}

int TestModel::revision() {
    return 2;
}

bool TestModel::migrate(int revision) {
    Builder* b = getBuilder()->clear();
    
    switch (revision) {
        case 1:
            {
                Ref<FieldDescriptor> f1 = b->getFieldDescriptor("id", "INT");
                f1.getPtr()->setUnsigned(true).setIndex("PRIMARY").setAutoIncrement();
                
                Ref<FieldDescriptor> f2 = b->getFieldDescriptor("email", "VARCHAR");
                f2.getPtr()->setParameter("64").notNull(true);
                
                b->fieldDescriptor(f1);
                b->fieldDescriptor(f2);
                b->setEngine("InnoDB").setCharset("utf8mb4").setCollate("utf8mb4_0900_ai_ci");
                
                con->execute(b->sql(Builder::CREATE));
            }
            return true;
        
        case 2:
            {
                Ref<FieldDescriptor> f1 =
                Ref<FieldDescriptor>(new mysql::AlterTableField(
                        ((mysql::FieldDescriptor*)b->getFieldDescriptor("description", "VARCHAR").getPtr())
                                    ));
                                                
                ((mysql::AlterTableField*)f1.getPtr())->add().afterColumn("email").setParameter("64").notNull(true);
                
                b->fieldDescriptor(f1);
                
                con->execute(b->sql(Builder::ALTER));
            }
            return true;
        
    }
    
    return false;
}

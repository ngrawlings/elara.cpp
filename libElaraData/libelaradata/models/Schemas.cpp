//
//  Schemas.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 14/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Schemas.h"

#include <libelaracore/exception/Exception.h>

#include <libelaradata/connectors/Connector.h>

#include <libelaradata/sql/sections/sql/ClauseValue.h>

namespace nrcore {
    
    Schemas::Schemas(Connector* con) : con(con) {
        if (!exists())
            create();
    }
    
    Schemas::~Schemas() {
        
    }
    
    void Schemas::setConnection(Connector* con) {
        this->con = con;
    }
    
    int Schemas::getRevision(String table) {
        Ref<Builder> _b = con->getBuilder("schemas");
        Builder* b = _b.getPtr();
        
        b->select("revision");
        b->setClause(Ref<Clause>(new ClauseValue(b, "table", "=", table)));
        
        ResultSet res = con->query(b->sql(Builder::SELECT));
        if (res.length())
            return res.row(0).getInteger(0);
        
        return -1;
    }
    
    void Schemas::setRevision(String table, int revision) {
        int _rev = getRevision(table);
        
        Ref<Builder> _b = con->getBuilder("schemas");
        Builder* b = _b.getPtr();
        
        b->value("revision", String("%").arg(revision));
        b->setClause(Ref<Clause>(new ClauseValue(b, "table", "=", table)));
        
        if (_rev != -1)
            con->execute(b->sql(Builder::UPDATE));
        else {
            b->value("table", String("%").arg(table));
            con->execute(b->sql(Builder::INSERT));
        }
    }
    
    bool Schemas::exists() {
        try {
            con->query(String("SELECT `id` FROM `schemas` LIMIT 1;"));
        } catch (Exception e) {
            return false;
        }
        return true;
    }
    
    void Schemas::create() {
        Ref<Builder> refb = con->getBuilder("schemas");
        Builder* b = refb.getPtr();
        
        Ref<FieldDescriptor> f1 = b->getFieldDescriptor("id", "INT");
        f1.getPtr()->setUnsigned(true);
        f1.getPtr()->setIndex("PRIMARY");
        f1.getPtr()->setAutoIncrement();
        
        Ref<FieldDescriptor> f2 = b->getFieldDescriptor("table", "VARCHAR");
        f2.getPtr()->setParameter("32");
        f2.getPtr()->notNull(true);
        
        Ref<FieldDescriptor> f3 = b->getFieldDescriptor("revision", "INT");
        f3.getPtr()->notNull(true);
        f3.getPtr()->setDefault("0");
        
        b->fieldDescriptor(f1);
        b->fieldDescriptor(f2);
        b->fieldDescriptor(f3);
        b->setEngine("InnoDB");
        b->setCharset("utf8mb4");
        b->setCollate("utf8mb4_0900_ai_ci");
        
        con->execute(b->sql(Builder::CREATE));
    }
    
}

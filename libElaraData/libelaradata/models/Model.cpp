//
//  Model.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 14/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Model.h"

#include <libelaracore/exception/Exception.h>
#include <libelaracore/memory/Ref.h>

#include <libelaradata/sql/sections/sql/ClauseValue.h>

namespace elara {
 
    Model::Model(Connector* con, String table) : con(con), table(table) {
        loadRevision();
        
        builder = Ref<Builder>();
    }
    
    Model::~Model() {
        
    }
    
    void Model::loadRevision() {
        _revision = con->schemas().getPtr()->getRevision(table);
    }
    
    void Model::runMigration() {
        int current_revision = revision();
        for (int i=_revision+1; i<=current_revision; i++) {
            try {
                if (migrate(i))
                    con->schemas().getPtr()->setRevision(table, i);
            } catch (Exception e) {
                printf("Error in data migration: %s\n", e.getMessage());
            }
        }
    }
    
    Builder* Model::getBuilder() {
        if (con)
            builder = con->getBuilder(table);
        
        if (!builder.getPtr())
            throw Exception(-1, "Failed to get a builder instance");
        
        return builder.getPtr();
    }
    
}

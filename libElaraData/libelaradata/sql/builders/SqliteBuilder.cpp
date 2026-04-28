//
//  SqliteBuilder.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 17/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "SqliteBuilder.h"

#include <libelaracore/exception/Exception.h>
#include "../sections/sqlite/FieldDescriptor.h"

namespace elara {
    
    SqliteBuilder::SqliteBuilder(String name) : Builder(name) {
        
    }
    
    SqliteBuilder::~SqliteBuilder() {
        
    }
    
    String SqliteBuilder::escape(String str) {
        return str;
    }
    
    String SqliteBuilder::sql(TYPE type) {
        switch(type) {
            case SELECT:
                return select();
            
            case INSERT:
                return insert();
            
            case UPDATE:
                return update();
            
            case DELETE:
                return _delete();
            
            case CREATE:
                return create();
            
            case ALTER:
                return alter();
            
            case DROP:
                return drop();
                
            case TRUNCATE:
                return truncate();

        }
        
        throw Exception(-1,  "Reached non executable point");
    }
    
    Ref<FieldDescriptor> SqliteBuilder::getFieldDescriptor(String name, String type) {
        return Ref<FieldDescriptor>(new sqlite::FieldDescriptor(name, sqlite::FieldDescriptor::getTypeByString(type)));
    }
    
    String SqliteBuilder::select() {
        return "";
    }
    
    String SqliteBuilder::insert() {
        return "";
    }
    
    String SqliteBuilder::update() {
        return "";
    }
    
    String SqliteBuilder::_delete() {
        return "";
    }
    
    String SqliteBuilder::create() {
        return "";
    }
    
    String SqliteBuilder::alter() {
        return "";
    }
    
    String SqliteBuilder::drop() {
        return "";
    }
    
    String SqliteBuilder::truncate() {
        return "";
    }

}

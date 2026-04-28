//
//  AlterTableField.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 14/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "AlterTableField.h"

namespace elara {
    namespace mysql {
        
        AlterTableField::AlterTableField(String name, FieldDescriptor::TYPE type) : FieldDescriptor(name, type) {
            
        }
        
        AlterTableField::AlterTableField(const AlterTableField &atf) : FieldDescriptor(atf) {
            action = atf.action;
            new_name = atf.new_name;
            after_column = atf.after_column;
        }
        
        AlterTableField::AlterTableField(FieldDescriptor *fd) : FieldDescriptor(*fd) {
            
        }
        
        AlterTableField::~AlterTableField() {
            
        }
        
        AlterTableField& AlterTableField::add() {
            action = ADD;
            return *this;
        }
        
        AlterTableField& AlterTableField::drop() {
            action = DROP;
            return *this;
        }
        
        AlterTableField& AlterTableField::change(String new_name) {
            action = CHANGE;
            this->new_name = new_name;
            return *this;
        }
        
        AlterTableField& AlterTableField::afterColumn(String field) {
            after_column = field;
            return *this;
        }
        
        String AlterTableField::toString() {
            String ret;
            
            switch (action) {
                case ADD:
                    ret = String("ADD COLUMN %").arg(FieldDescriptor::toString());
                    break;
                    
                case DROP:
                    ret = String("DROP COLUMN `%`").arg(name);
                    break;
                    
                case CHANGE:
                    ret = String("CHANGE COLUMN `%` `%` %").arg(name).arg(new_name.length() ? new_name : name).arg(FieldDescriptor::toString());
                    break;
            }
            
            ret.arg(FieldDescriptor::toString());
            
            if (after_column.length())
                ret += String(" AFTER %").arg(after_column);
            else
                ret += " FIRST";
            
            return ret;
        }
        
    }
}

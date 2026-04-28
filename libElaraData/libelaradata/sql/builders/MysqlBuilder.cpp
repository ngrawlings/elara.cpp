//
//  MysqlGenerator.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "MysqlBuilder.h"

#include <libelaracore/exception/Exception.h>
#include "../sections/mysql/FieldDescriptor.h"

namespace elara {
    
    MysqlBuilder::MysqlBuilder(MYSQL *mysql, String name) : Builder(name), mysql(mysql) {
        
    }
    
    MysqlBuilder::MysqlBuilder(const MysqlBuilder& gen) : Builder(gen), mysql(gen.mysql) {
        
    }
    
    MysqlBuilder::~MysqlBuilder() {
        
    }
    
    String MysqlBuilder::escape(String str) {
        char *to = new char[(str.length()*2)+1];
        unsigned long len = mysql_real_escape_string(mysql, to, str.operator char *(), str.length());
        
        String ret(to, len);
        
        delete [] to;
        return ret;
    }
    
    String MysqlBuilder::sql(TYPE type) {
        switch(type) {
            case SELECT:
                return _select();
                
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
    
    Ref<FieldDescriptor> MysqlBuilder::getFieldDescriptor(String name, String type) {
        return Ref<FieldDescriptor>(new mysql::FieldDescriptor(name, mysql::FieldDescriptor::getTypeByString(type)));
    }
    
    String MysqlBuilder::_select() {
        String fields = this->fields.toString();
        String joins = this->getJoins();
        String where = this->clause.getPtr() ? this->clause.getPtr()->toString() : "";
        String order = this->_order.toString();
        String group = this->_group.toString();
        String limit = this->offset_limit.toString();
        
        String sql = String("SELECT %% FROM `%`")
                .arg(_distinct ? "DISTINCT " : "").arg(fields).arg(table);
        
        if (this->joins.length())
            sql += String(" %").arg(joins);
        
        if (where.length())
            sql += String(" WHERE %").arg(where);
        
        if (order.length())
            sql += String(" ORDER BY %").arg(order);
        
        if (group.length())
            sql += String(" GROUP BY %").arg(group);
        
        if (limit.length())
            sql += String(" %").arg(limit);
        
        return sql;
    }
    
    String MysqlBuilder::insert() {
        String fields = this->values.getFields();
        String values = this->values.getValues();

        return String("INSERT INTO `%` (%) VALUES (%)").arg(table).arg(fields).arg(values);
    }
    
    String MysqlBuilder::update() {
        String values = this->values.toString();
        String where = this->clause.getPtr() ? this->clause.getPtr()->toString() : "";
        String limit = this->offset_limit.toString();
        
        String sql = String("UPDATE `%` SET %").arg(table).arg(values);
        
        if (where.length())
            sql += String(" WHERE %").arg(where);
        
        if (limit.length())
            sql += String(" LIMIT %").arg(limit);
        
        return sql;
    }
    
    String MysqlBuilder::_delete() {
        String where = this->clause.getPtr() ? this->clause.getPtr()->toString() : "";
        return String("DELETE FROM `%` WHERE %;").arg(table).arg(where);
    }
    
    String MysqlBuilder::create() {
        int len = (int)field_descriptors.length();
        if (len) {
            String fields = field_descriptors[0].getPtr()->toString();
            String indexes = ((mysql::FieldDescriptor*)field_descriptors[0].getPtr())->getIndex();
            
            for (int i=1; i<len; i++) {
                fields += String(", ")+field_descriptors[i].getPtr()->toString();
                
                String index = ((mysql::FieldDescriptor*)field_descriptors[i].getPtr())->getIndex();
                if (index.length())
                    indexes += String(", ")+index;
            }
            
            return String("CREATE TABLE `%` (%, %) ENGINE=% DEFAULT CHARSET=% COLLATE=%;")
                    .arg(table).arg(fields).arg(indexes).arg(engine).arg(charset).arg(collate);
        }
        
        throw Exception(-1, "No field descriptors");
    }
    
    String MysqlBuilder::alter() {
        int len = (int)field_descriptors.length();
        if (len) {
            String fields = field_descriptors[0].getPtr()->toString();
 
            for (int i=1; i<len; i++)
                fields += String(", ")+field_descriptors[i].getPtr()->toString();
            
           return String("ALTER TABLE `%` %;").arg(table).arg(fields);
        }
        
        throw Exception(-1, "No field descriptors");
    }
    
    String MysqlBuilder::drop() {
        return String("DROP TABLE `%`;").arg(table);
    }

    String MysqlBuilder::truncate() {
        return String("TRUNCATE TABLE `%`;").arg(table);
    }
    
}

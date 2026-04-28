//
//  Generator.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Builder.h"

namespace nrcore {
 
    Builder::Builder(String table) : table(table) {
        clear();
    }
    
    Builder::Builder(const Builder& genbase) {
        table               = genbase.table;
        fields              = genbase.fields;
        joins               = genbase.joins;
        values              = genbase.values;
        clause              = genbase.clause;
        _order              = genbase._order;
        _group              = genbase._group;
        offset_limit        = genbase.offset_limit;
        
        _distinct            = genbase._distinct;
        
        field_descriptors   = genbase.field_descriptors;
        engine              = genbase.engine;
        charset             = genbase.charset;
        collate             = genbase.collate;
    }
    
    Builder::~Builder() {
        
    }
    
    Builder* Builder::clear() {
        fields.clear();
        joins.clear();
        values.clear();
        clause = Ref<Clause>();
        _order.clear();
        _group.clear();
        offset_limit = OffsetLimit();
        
        _distinct = false;
        
        field_descriptors.clear();
        
        return this;
    }
    
    String Builder::getJoins() {
        String ret;
        
        int len = (int)this->joins.length();
        if (len) {
            ret = this->joins[0].getPtr()->toString()+" ";
            for(int i=1; i<len; i++) {
                ret += this->joins[i].getPtr()->toString()+" ";
            }
        }
        
        return ret;
    }
    
    Builder* Builder::select(String field) {
        fields.addField(escape(field));
        return this;
    }
    
    Builder* Builder::distinct(bool val) {
        _distinct = val;
        return this;
    }
    
    Builder* Builder::join(String table, Ref<Clause> clause) {
        joins.push(Ref<Join>(new Join(escape(table), clause)));
        return this;
    }
    
    Builder* Builder::leftJoin(String table, Ref<Clause> clause) {
        joins.push(Ref<Join>(new Join(escape(table), clause, "LEFT")));
        return this;
    }
    
    Builder* Builder::rightJoin(String table, Ref<Clause> clause) {
        joins.push(Ref<Join>(new Join(escape(table), clause, "RIGHT")));
        return this;
    }
                           
    Builder* Builder::outerJoin(String table, Ref<Clause> clause) {
        joins.push(Ref<Join>(new Join(escape(table), clause, "OUTER")));
        return this;
    }
    
    Builder* Builder::value(String name, String value, int flags) {
        values.add(escape(name), escape(value), flags);
        return this;
    }
    
    Builder* Builder::setClause(Ref<Clause> clause) {
        this->clause = clause;
        return this;
    }
    
    Builder* Builder::order(String field, Order::DIRECTION direction) {
        _order.add(escape(field), direction);
        return this;
    }
    
    Builder* Builder::group(String field) {
        _group.addField(escape(field));
        return this;
    }
    
    Builder* Builder::offset(long long offset) {
        offset_limit.offset(offset);
        return this;
    }
    
    Builder* Builder::limit(int limit) {
        offset_limit.limit(limit);
        return this;
    }
    
    Builder* Builder::fieldDescriptor(Ref<FieldDescriptor> fd) {
        field_descriptors.push(fd);
        return this;
    }
    
    Builder& Builder::setEngine(String engine) {
        this->engine = escape(engine);
        return *this;
    }
    
    Builder& Builder::setCharset(String charset) {
        this->charset = escape(charset);
        return *this;
    }
    
    Builder& Builder::setCollate(String collate) {
        this->collate = escape(collate);
        return *this;
    }
    
}

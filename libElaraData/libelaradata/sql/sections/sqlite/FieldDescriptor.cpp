//
//  FieldDescriptor.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 04/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "FieldDescriptor.h"

#include <libelaracore/exception/Exception.h>

namespace nrcore {
    namespace sqlite {
        
        const char* FieldDescriptor::field_types[] = {"TINYINT", "SMALLINT","MEDIUMINT","INT",
            "BIGINT","FLOAT","DOUBLE","DOUBLE_PRECISION","REAL","DECIMAL", 0};
        
        FieldDescriptor::FieldDescriptor(String name, TYPE type) {
            this->name = name;
            this->type = type;
            
            _unsigned = false;
            zerofill = false;
            binary = false;
            notnull = false;
        }
        
        FieldDescriptor::FieldDescriptor(const FieldDescriptor& fd) {
            name        = fd.name;
            type        = fd.type;
            parameter   = fd.parameter;
            _unsigned   = fd._unsigned;
            zerofill    = fd.zerofill;
            binary      = fd.binary;
            notnull     = fd.notnull;
            _default    = fd._default;
            extra       = fd.extra;
            encoding    = fd.encoding;
            collation   = fd.collation;
            comment     = fd.comment;
            index       = fd.index;
        }
        
        FieldDescriptor::~FieldDescriptor() {
            
        }
        
        FieldDescriptor& FieldDescriptor::setParameter(String parameter) {
            this->parameter = parameter;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setUnsigned(bool val) {
            this->_unsigned = val;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setZeroFill(bool val) {
            this->zerofill = val;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setBinary(bool val) {
            this->binary = val;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::notNull(bool val) {
            this->notnull = val;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setDefault(String _default) {
            this->_default = _default;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setExtra(String extra) {
            this->extra = extra;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setAutoIncrement() {
            this->extra = String("AUTO_INCREMENT");
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setUpdateCurrentDate() {
            this->extra = String("on update CURRENT_TIMESTAMP");
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setSerialDefaultValue() {
            this->extra = String("on update CURRENT_TIMESTAMP");
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setEncoding(String encoding) {
            this->extra = String("SERIAL DEFAULT VALUE");
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setCollation(String collation) {
            this->collation = collation;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setComment(String comment) {
            this->comment = comment;
            return *this;
        }
        
        FieldDescriptor& FieldDescriptor::setIndex(INDEX_TYPE type) {
            _INDEX *idx = new _INDEX;
            idx->type = type;
            idx->name = name;
            
            this->index = Ref<_INDEX>(idx);
            return *this;
        }
        
        String FieldDescriptor::getTypeString(TYPE type) {
            switch(type) {
                case TINYINT:
                    return "TINYINT";
                case SMALLINT:
                    return "SMALLINT";
                case MEDIUMINT:
                    return "MEDIUMINT";
                case INT:
                    return "INT";
                case BIGINT:
                    return "BIGINT";
                case FLOAT:
                    return "FLOAT";
                case DOUBLE:
                    return "DOUBLE";
                case DOUBLE_PRECISION:
                    return "DOUBLE PRECISION";
                case REAL:
                    return "REAL";
                case DECIMAL:
                    return "DECIMAL";
                case BIT:
                    return "BIT";
                case SERIAL:
                    return "SERIAL";
            }
        }
        
        FieldDescriptor::TYPE FieldDescriptor::getTypeByString(String type) {
            int index = 0;
            while (field_types[index]) {
                if (type == String(field_types[index]))
                    return (FieldDescriptor::TYPE)index;
                index++;
            }
            
            throw Exception(-1, "Invalid");
        }
        
        String FieldDescriptor::toString() {
            String ret = String("`%` %").arg(name).arg(getTypeString(type));
            
            if (parameter.length())
                ret = String(ret+"(%)").arg(parameter);
            
            if (_unsigned)
                ret += " unsigned";
            
            if (zerofill)
                ret += " zerofill";
            
            if (binary)
                ret += " binary";
            
            if (notnull)
                ret += " NOT NULL";
            
            if (_default)
                ret += String(" DEFAULT %").arg(_default);
            
            if (extra)
                ret += String(" %").arg(extra);
            
            if (encoding)
                ret += String(" %").arg(encoding);
            
            if (collation)
                ret += String(" %").arg(collation);
            
            if (comment)
                ret += String(" COMMENT '%'").arg(comment);
            
            return ret;
        }
        
        String FieldDescriptor::getIndex() {
            if (index.getPtr()) {
                switch (index.getPtr()->type) {
                    case PRIMARY:
                        return String("PRIMARY KEY (`%`)").arg(index.getPtr()->name);
                        
                    case INDEX:
                        return String("KEY (`%`)").arg(index.getPtr()->name);
                        
                    case UNIQUE:
                        return String("UNIQUE KEY (`%`)").arg(index.getPtr()->name);
                        
                    case FULLTEXT:
                        return String("FULLTEXT KEY (`%`)").arg(index.getPtr()->name);
                }
            }
            return "";
        }
        
    }
}

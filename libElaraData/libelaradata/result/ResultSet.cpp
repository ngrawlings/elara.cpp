//
//  ResultSet.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "ResultSet.h"
#include <libelaracore/exception/Exception.h>

namespace elara {
    
    ResultSet::ResultSet(Connector *con, Array<String> columns, unsigned int row_count) : con(con), columns(columns), rows(row_count) {
        cursor_offset = 0;
    }
    
    ResultSet::~ResultSet() {
        
    }
    
    void ResultSet::addRow(Array<Memory> fields) {
        if (fields.length() != columns.length())
            throw Exception(-1, "Column count mis-match");
        
        rows.push(Ref<Row>(new Row(this, fields)));
    }
    
    Row ResultSet::row(unsigned int offset) {
        return rows[offset].get();
    }
    
    Row ResultSet::first() {
        cursor_offset = 0;
        return rows[0].get();
    }
    
    Row ResultSet::last() {
        cursor_offset = (unsigned int)rows.length()-1;
        return rows[cursor_offset].get();
    }
    
    Row* ResultSet::next() {
        if (cursor_offset<rows.length())
            return rows[cursor_offset++].getPtr();

        return 0;
    }
    
    int ResultSet::getColumnIndex(String name) {
        int len = (int)columns.length();
        for (int i=0; i<len; i++) {
            if (columns[i] == name)
                return i;
        }
        return -1;
    }
    
    size_t ResultSet::length() {
        return rows.length();
    }
    
}

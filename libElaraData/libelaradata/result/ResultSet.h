//
//  ResultSet.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef ResultSet_hpp
#define ResultSet_hpp

#include "Row.h"

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Array.h>

namespace nrcore {
    
    class Connector;
    
    class ResultSet {
    public:
        ResultSet(Connector *con, Array<String> columns, unsigned int row_count=0);
        virtual ~ResultSet();
        
        void addRow(Array<Memory> fields);
        Row row(unsigned int offset);
        Row first();
        Row last();
        Row* next();
        
        int getColumnIndex(String name);
        
        size_t length();
        
    protected:
        Connector *con;
        Array<String> columns;
        Array< Ref<Row> > rows;
        
        unsigned int cursor_offset;
        
    };
    
}


#endif /* ResultSet_hpp */

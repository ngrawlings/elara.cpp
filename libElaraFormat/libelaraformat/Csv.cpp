//
//  Csv.cpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 09/01/2020.
//  Copyright © 2020 Liquidsoft Studio. All rights reserved.
//

#include "Csv.h"

namespace nrcore {

    Csv::Csv() {
        cursor = 0;
    }

    Csv::~Csv() {
        
    }

    bool Csv::loadData(String data) {
        this->data = data;
        cursor = 0;
        return false;
    }

    bool Csv::loadFile(String path) {
        return false;
    }

    bool Csv::eof() {
        return cursor == data.length();
    }

    StringList Csv::getEntry() {
        if (!eof()) {
            int index = data.indexOf("\n", cursor);
            
            if (index != -1) {
                String line = data.substr(cursor, index-cursor);
                cursor += index+1;
                
                return StringList(line, ",");
            } else {
                String line = data.substr(cursor);
                cursor = (int)data.length();
                return StringList(line, ",");
                
            }
        }
        throw "eof";
    }

}

//
//  Csv.hpp
//  libElaraFormat
//
//  Created by Nyhl Rawlings on 09/01/2020.
//  Copyright © 2020 Liquidsoft Studio. All rights reserved.
//

#ifndef Csv_hpp
#define Csv_hpp

#include <stdio.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/StringList.h>

namespace nrcore {

    class Csv {
    public:
        Csv();
        virtual ~Csv();
        
        bool loadData(String data);
        bool loadFile(String path);
        
        bool eof();
        StringList getEntry();
        
    protected:
        String data;
        int cursor;
    };

}

#endif /* Csv_hpp */

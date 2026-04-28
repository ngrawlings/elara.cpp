//
//  Result.hpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 01/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Result_hpp
#define Result_hpp

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>

namespace elara {
    
    class ResultSet;
    
    class Row {
    public:
        Row(ResultSet *result_set, Array<Memory> values);
        Row(const Row &result);
        virtual ~Row();
        
        String getString(int index);
        String getString(String name);
        
        long long getInteger(int index);
        long long getInteger(String name);
        
        unsigned long long getUnsignedInteger(int index);
        unsigned long long getUnsignedInteger(String name);
        
        double getDouble(int index);
        double getDouble(String name);
        
        Memory getBlob(int index);
        Memory getBlob(String name);
        
    protected:
        ResultSet *result_set;
        Array<Memory> values;
    };
    
}

#endif /* Result_hpp */

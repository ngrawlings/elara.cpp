//
//  RefArray.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 15/04/2014.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#ifndef libElaraCore_RefArray_h
#define libElaraCore_RefArray_h

#include "Ref.h"

namespace elara {

    template <class T>
    class RefArray : public Ref<T> {
    public:
        explicit RefArray<T>(T* ptr) : Ref<T>(ptr, true) {
        }
        
        RefArray<T>(const RefArray<T>& ref) : Ref<T>(ref) {
        }
        
        RefArray<T>() : Ref<T>(0) {
        }
        
    };
    
};

#endif

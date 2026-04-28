//
//  ThreadSafeObject.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 9/21/13.
//  Copyright (c) 2013 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__ThreadSafeObject__
#define __libElaraCore__ThreadSafeObject__

#include "Mutex.h"

namespace nrcore {
    
    template <class T>
    class ThreadSafeObject {
    public:
        ThreadSafeObject(T obj) {
            this->obj = obj;
        }
        virtual ~ThreadSafeObject() {}
        
        T& acquire() {
            mutex.lock();
            return obj;
        }
        
        void release() {
            mutex.release();
        }
        
    private:
        T obj;
        Mutex mutex;
    };

};

#endif /* defined(__libElaraCore__ThreadSafeObject__) */

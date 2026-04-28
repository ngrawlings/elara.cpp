//
//  Mutex.cpp
//  libElaraCore
//
//  Created by Nyhl Rawlings on 12/02/2013.
//  Copyright (c) 2013. All rights reserved.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// For affordable commercial licensing please contact nyhl@ngrawlings.com
//

#include "Mutex.h"

#include <libelaracore/types.h>

#if __APPLE__
#include <sys/time.h>
#endif

#include <assert.h>
#include <sys/errno.h>

#include "Thread.h"

namespace nrcore {

    Mutex::Mutex(const char *tag, bool manage) {
        this->_tag = tag;
        this->lock_tag = 0;
        this->manage = manage;
        owner = 0;
        pthread_mutex_init(&mutex, 0);
    }

    Mutex::~Mutex() {
        if (isLocked() && !isLockedByMe() && !pthread_kill(owner, 0))
            lock();
        
        if (isLockedByMe())
            release();
        
        pthread_mutex_destroy(&mutex);
    }

    bool Mutex::lock(long timeout, const char* lock_tag) {
        if (owner == pthread_self())
            assert("deadlock");
        
        if (timeout==0) {
            if (pthread_mutex_lock(&mutex))
                throw "Error: Mutex Lock";
            owner = pthread_self();
            this->lock_tag = lock_tag;
            
            return true;
        } else {
    #if defined(__APPLE__) || defined(__ANDROID__)
            timespec start, current;
            clock_gettime(CLOCK_MONOTONIC, &start);
            long elapse;
            while (true) {
                    
                if (tryLock(lock_tag)) {
                    owner = pthread_self();
                    return true;
                }

                clock_gettime(CLOCK_MONOTONIC, &current);
                elapse = (start.tv_sec - current.tv_sec) * 1000;
                elapse += (start.tv_nsec - current.tv_nsec) / 1000000;
                
                if (elapse >= timeout)
                    return false;
                    
                    
                usleep(1000);
            }
    #else
            struct timespec to;
            to.tv_sec = timeout/1000;
            to.tv_nsec = (timeout%1000)*1000000;
            if (!pthread_mutex_timedlock(&mutex, &to)) {
                owner = pthread_self();
                this->lock_tag = lock_tag;
                
                if (manage)
                    Thread::mutexLocked(this);
                
                owner = pthread_self();
                return true;
            }
    #endif
            
        }
        return false;
    }

    void Mutex::wait(ThreadWaitCondition *cond, int nsecs) {
        if (!nsecs) {
            lock();
            cond->wait(&mutex);
            owner = pthread_self();
        } else {
            struct timespec tm;
            tm.tv_nsec = nsecs;
            tm.tv_sec = 0;
            if (cond->timedWait(&mutex, &tm))
                owner = pthread_self();
        }
    }

    bool Mutex::tryLock(const char* lock_tag) {
        if (owner == pthread_self())
            return false;
        
        if (pthread_mutex_trylock(&mutex))
            return false;

        owner = pthread_self();
        this->lock_tag = lock_tag;
  
        return true;
    }

    void Mutex::release() {
        if (!isLockedByMe()) {
            assert(false);
            return;
        }
        
        owner = 0;
        
        int res = pthread_mutex_unlock(&mutex);
        if (res) {
            assert(false);
            return;
        }
    }

    bool Mutex::isLocked() {
        return owner && owner != (thread_t)-1;
    }

    bool Mutex::isLockedByMe() {
        return owner == pthread_self();
    }

    thread_t Mutex::getOwner() {
        return owner;
    }

    const char* Mutex::tag() {
        return _tag;
    }

    const char* Mutex::lockTag() {
        return lock_tag;
    }

    bool Mutex::isManaged() {
        return manage;
    }

}

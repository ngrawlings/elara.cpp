//
//  ThreadWaitCondition.cpp
//  libElaraCore
//
//  Created by Nyhl Rawlings on 23/03/2013.
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

#include "ThreadWaitCondition.h"
#include <sys/errno.h>

namespace elara {

    ThreadWaitCondition::ThreadWaitCondition() {
        pthread_cond_init(&condition, 0);
        count = 0;
    }

    ThreadWaitCondition::~ThreadWaitCondition() {
        pthread_cond_destroy(&condition);
    }

    void ThreadWaitCondition::trigger() {
        int ccnt = count;
        while (ccnt && ccnt == count) {
            pthread_cond_signal(&condition);
            if (ccnt == count)
                usleep(100);
        }
    }

    void ThreadWaitCondition::broadcast() {
        while (count) {
            pthread_cond_broadcast(&condition);
            usleep(100);
        }
    }

    void ThreadWaitCondition::wait(thread_mutex_t *mutex) {
        count++;
        pthread_cond_wait(&condition, mutex);
        count--;
    }
    
    bool ThreadWaitCondition::timedWait(thread_mutex_t *mutex, timespec *tm) {
        count++;
        if (pthread_cond_timedwait(&condition, mutex, tm) == ETIMEDOUT) {
            count--;
            return false;
        }
        return true;
    }

}

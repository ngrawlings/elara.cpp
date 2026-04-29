//
//  Thread.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 19/11/2012.
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
// For affordable commercial licensing please contact ngrawlings@gmail.comm
//

#ifndef __Elara__Thread__
#define __Elara__Thread__

#include <pthread.h>
#include <signal.h>

#include "Task.h"

#include <libelaracore/exception/Exception.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/LinkedList.h>

#include "ThreadWaitCondition.h"
#include "Mutex.h"

namespace elara {

    class Thread {
    public:
        friend class Mutex;
        
        enum THREAD_STATUS {
            THREAD_ACTIVE,
            THREAD_WAITING
        };
        
    protected:
        Thread();
        virtual ~Thread();
        
    public:
        static bool pool;
        static int max_threads;
        
        static void init(int thread_count);
        
        static Thread *addThread();
        static Thread *runTask(Task *task);
        static Thread *getWaitingThread();
        THREAD_STATUS getStatus() { return status; }
        static void stopAllThreads();
        static void staticCleanUp();
        
        static int getCount() { return threads->length(); }
        static int getWaitCount() { return wait_threads->length(); }
        
        void waitUntilFinished();
        void signal(int sig);
        static void waitForAnyAvailableThread();
        
        static Thread* getThreadInstance();
        
        static void getThreadPoolState(int *total, int *active);
        
        void queueTaskToCurrentThread(Task *task);
        
        void wake();
        
    protected:
        void thread_loop();
        
        void finished();
        
        Task *getNextTask();
        
        bool _run;
        
        static void mutexLocked(Mutex* mutex);
        static void mutexReleased(Mutex* mutex);
        
        virtual void threadStarted() {}
        virtual void threadFinished() {}

        LinkedList<Mutex*> locked_mutex_list;

    private:
        thread_t thread;
        ThreadWaitCondition trigger;
        Mutex mutex;
        THREAD_STATUS status;
        LINKEDLIST_NODE_HANDLE linkedlist_node_hanble;
        
        ThreadWaitCondition wait_for_thread_trigger;
        Mutex wait_for_thread_finish;
        
        LinkedList<Task*> task_queue;
        
        static void *threadEntry( void *inst );
        
        void wait();
        
        // List of pooled threads
        static LinkedList<Thread*> *threads;
        static Mutex *threads_mutex;
        
        static LinkedList<Thread*> *wait_threads;
        static Mutex *wait_threads_mutex;

        static ThreadWaitCondition *wait_any_thread_trigger;
        static Mutex *wait_any_thread_mutex;
        
        static thread_key_t tlsKey;
    };
    
}

namespace elara {
namespace threading {

using ::elara::Thread;

}
}

#endif /* defined(__Elara__Thread__) */

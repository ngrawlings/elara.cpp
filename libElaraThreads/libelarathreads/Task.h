//
//  Task.h
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
// For affordable commercial licensing please contact ngrawlings@gmail.comm
//

#ifndef __Elara__Task__
#define __Elara__Task__

#include <pthread.h>
#include <libelaracore/memory/LinkedList.h>
#include <libelarathreads/memory/Ref.h>
#include "Mutex.h"

namespace elara {

    class Thread;
    
    class Task {
    public:
        friend class Thread;
        
        Task();
        virtual ~Task();

        static bool taskExists(elara::threading::memory::Ref<Task> task);
        static void queueTask(elara::threading::memory::Ref<Task> task);
        static void removeTask(Task* task);
        static void removeTask(elara::threading::memory::Ref<Task> task);
        static elara::threading::memory::Ref<Task> getNextTask();
        
        
        static int getQueuedTaskCount();
        
        static void shuttingDown();
        static void staticInit();
        static void staticCleanup();
        
        Thread* getAquiredThread();
        
        bool isFinished() { return task_finished; }

    protected:
        virtual void run() = 0;
        void finished() { task_finished = true; }
        void reset() { task_finished = false; }
        
        static bool _taskExists(elara::threading::memory::Ref<Task> task);
        
        unsigned long getThreadId();

        bool task_finished;

    private:
        static bool shutting_down;
        static Mutex *task_queue_mutex;
        static LinkedList< elara::threading::memory::Ref<Task> > *task_queue;
        
        Thread *acquired_thread; // thread instance pointer that is currently executing this task, or null if not running
    };

}

namespace elara {
namespace threading {

using ::elara::Task;

}
}

#endif /* defined(__Elara__Task__) */

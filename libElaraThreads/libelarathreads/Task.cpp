//
//  Task.cpp
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

#include "Task.h"

#include <assert.h>
#include <libelarathreads/Thread.h>

namespace nrcore {

    bool Task::shutting_down = false;
    Mutex *Task::task_queue_mutex = 0;
    LinkedList<Task*> *Task::task_queue = 0;

    Task::Task() {
        this->dynamicly_allocated = false;
        acquired_thread = 0;
        task_finished = false;
    }
    
    Task::Task(bool dynamicly_allocated) {
        this->dynamicly_allocated = dynamicly_allocated;
        acquired_thread = 0;
        task_finished = false;
    }

    Task::~Task() {
        removeTasks(this);
        
        if ( acquired_thread && acquired_thread != Thread::getThreadInstance() )
            reinterpret_cast<Thread*>(acquired_thread)->waitUntilFinished();
    }
    
    bool Task::taskExists(Task *task) {
        bool ret;
        task_queue_mutex->lock();
        ret = _taskExists(task);
        task_queue_mutex->release();
        return ret;
    }
    
    bool Task::_taskExists(Task *task) {
        LinkedListState<Task*> tq(task_queue);
        int cnt = tq.length();
        while (cnt--) {
            if (tq.next() == task)
                return true;
        }
        return false;
    }

    void Task::queueTask(Task *task) {
        if (!shutting_down) {
            task->reset();
            task_queue_mutex->lock();
            if (!_taskExists(task))
                task_queue->add(task);
            task_queue_mutex->release();
        }
    }

    void Task::removeTasks(Task *task) {
        task_queue_mutex->lock();
        
        LINKEDLIST_NODE_HANDLE node;
        
        if (task_queue->length()) {
            node = task_queue->firstNode();
            
            if (node) {
                do {
                    
                    if (task_queue->get(node) == task) {
                        task_queue->removeNode(node);
                    
                        if (!task_queue->length())
                            break;
                    }
                    node = task_queue->nextNode(node);
                    
                } while (task_queue->length() && node != task_queue->firstNode());
            }
        }
        
        task_queue_mutex->release();
    }

    Task* Task::getNextTask() {
        Task *ret=0;
        
        try {
            task_queue_mutex->lock();
            if (task_queue->length()) {

                LINKEDLIST_NODE_HANDLE node = task_queue->firstNode();
                ret = task_queue->get(node);
                task_queue->removeNode(node);
            }
        } catch (...) {
        }
        
        if (ret)
            ret->acquired_thread = Thread::getThreadInstance();
        
        if (task_queue_mutex->isLockedByMe())
            task_queue_mutex->release();

        return ret;
    }
    
    bool Task::isDynamiclyAllocated() {
        return dynamicly_allocated;
    }

    unsigned long Task::getThreadId() {
        return (unsigned long)pthread_self();
    }

    void Task::staticInit() {
        task_queue_mutex = new Mutex("task_queue_mutex");
        task_queue = new LinkedList<Task*>();
    }

    void Task::staticCleanup() {
        delete task_queue_mutex;
        delete task_queue;
    }
    
    void Task::shuttingDown() {
        shutting_down = true;
        
        Task *task;
        while((task = getNextTask())) {
            task->run();
        }
    }
    
    Thread* Task::getAquiredThread() {
        return acquired_thread;
    }

    int Task::getQueuedTaskCount() {
        return task_queue->length();
    }
    
}

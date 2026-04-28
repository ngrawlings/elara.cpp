//
//  EventBase.cpp
//  libElaraCore
//
//  Created by Nyhl Rawlings on 10/03/14.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#include "EventBase.h"

#include <libelaracore/types.h>
#include <signal.h>
#include <libelaradebug/Log.h>

namespace elara {
    
    EventBase::EventBase() {
        event_signal = 0;
        evthread_use_pthreads();
        ev_base = event_base_new();
        thread = 0;
    }
    
    EventBase::~EventBase() {
        event_base_free(ev_base);
    }
    
    Thread* EventBase::getThread() {
        return thread;
    }
    
    void EventBase::run() {
        while (_run) {
            event_base_loop(ev_base, 4);
            usleep(10000);
        }
        LOG(Log::LOGLEVEL_NOTICE, "Event Loop Exiting");
    }
    
    void EventBase::runEventLoop(bool create_task) {
        _run = true;
        
        if (!create_task) {
            thread = 0;
            run();
        } else
            thread = Thread::runTask(this);
    }
    
    void EventBase::breakEventLoop() {
        _run = false;
        event_base_loopbreak(ev_base);
    }
    
    event_base* EventBase::getEventBase() {
        return ev_base;
    }
    
}

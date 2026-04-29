//
//  EventBase.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 10/03/14.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__EventBase__
#define __libElaraCore__EventBase__

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/event_struct.h>
#include <event2/event_compat.h>
#include <event2/thread.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>

namespace elara {
    
    class EventBase : public Task {
    public:
        EventBase();
        virtual ~EventBase();
        
        void runEventLoop(bool create_task=false);
        void breakEventLoop();
        
        event_base* getEventBase();
        
        Thread* getThread();
        
    protected:
        virtual void run();
        
    private:
        struct event_base *ev_base;
        struct event *event_signal;
        struct event *ev_schedule;
        
        Thread* thread;
        
        bool _run;

        elara::threading::memory::Ref<Task> self;
    };
    
};

namespace elara {
namespace event {
    using ::elara::EventBase;
}
}

#endif /* defined(__libElaraCore__EventBase__) */

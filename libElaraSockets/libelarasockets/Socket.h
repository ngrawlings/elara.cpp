//
//  Socket.hpp
//  decap
//
//  Created by Nyhl on 18/03/16.
//  Copyright © 2016 Liquidsoft Studio. All rights reserved.
//

#ifndef Socket_hpp
#define Socket_hpp

#include <libelaraevent/EventBase.h>
#include <libelaraevent/Timer.h>

#include "Address.h"
#include <libelarathreads/Mutex.h>

#include <libelaracore/memory/RingBuffer.h>

#include <errno.h>
#include <libelaracore/exception/Exception.h>

namespace elara {

    class Socket {
    public:
        friend class ReceiveTask;
        
        class CallbackInterface {
        public:
            friend class Socket;
            
        protected:
            virtual void onConnected(Socket *socket) {};
            virtual void onClosed(Socket *socket) {};
            virtual void onDestroyed(Socket *socket) {};
        };
        
        Socket(int _fd, CallbackInterface *cb=0);
        Socket(CallbackInterface *cb=0);
        virtual ~Socket();
        
        bool connect(Address address, unsigned short port);
        void poll();
        
        size_t available();
        Memory read(int max);
        
        size_t writeBufferSpace();
        int send(Memory data);
        int send(const char* buffer, size_t len);
        
        void setCallbackInterface(CallbackInterface *cb);
        
        void close();
        
        static void init(EventBase *event_base);
        
    protected:
        class ReceiveTask : public Task {
        public:
            friend class Socket;
            
            ReceiveTask(Socket *socket);
            virtual ~ReceiveTask();
            
            void run();
            
        private:
            Socket *socket;
        };
        
    protected:
        int fd;
        static EventBase *event_base;
        struct event *event_read, *event_write;
        
        Mutex recv_lock, send_lock;
        RingBuffer in_buffer, out_buffer;
        
        ReceiveTask recv_task;
        
        virtual void onReceive() = 0;
        virtual void onWriteReady() = 0;
        void receive();
        void sendReady();
        
    private:
        void enableEvents();
        
        static void ev_read(int fd, short ev, void *arg);
        static void ev_write(int fd, short ev, void *arg);
        
        CallbackInterface *cb_interface;
    };
    
}

namespace elara {
namespace sockets {

using ::elara::Socket;

}
}

#endif /* Socket_hpp */

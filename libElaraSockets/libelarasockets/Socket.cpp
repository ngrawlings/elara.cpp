//
//  Socket.cpp
//  decap
//
//  Created by Nyhl on 18/03/16.
//  Copyright © 2016 Liquidsoft Studio. All rights reserved.
//

#include "Socket.h"

#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <netinet/tcp.h>

#include <libelarathreads/Thread.h>
#include <libelarathreads/memory/Ref.h>

namespace elara {

    EventBase* Socket::event_base;
    
    Socket::Socket(int _fd, CallbackInterface *cb) : in_buffer(4096), out_buffer(4096), recv_task(this), cb_interface(cb) {
        this->fd = _fd;
        event_read = 0;
        event_write = 0;

        enableEvents();
        
        int flag = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
        
        if (cb_interface)
            cb_interface->onConnected(this);
    }

    Socket::Socket(CallbackInterface *cb) : in_buffer(4096), out_buffer(4096), recv_task(this), cb_interface(cb) {
        event_read = 0;
        event_write = 0;
    }

    Socket::~Socket() {
        close();
        
        if (cb_interface)
            cb_interface->onDestroyed(this);
    }
    
    bool Socket::connect(Address address, unsigned short port) {
        int type = address.getType() == Address::IPV4 ? AF_INET : AF_INET6;
        this->fd = socket(type, SOCK_STREAM, 0);
        if (this->fd == -1)
            return false;
        
        struct sockaddr_in ipa;
        memset(&ipa, 0, sizeof(sockaddr_in));
        ipa.sin_family = type;
        ipa.sin_port = htons(port);
        memcpy(&ipa.sin_addr.s_addr, address.getAddr(), type == AF_INET ? 4 : 16);
        
        int res = ::connect(this->fd, (const struct sockaddr *)&ipa, sizeof(sockaddr_in));
        if (res == -1)
            return false;
        
        enableEvents();
        
        int flag = 1;
        setsockopt(0, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
        
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
                return false;
        
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        
        if (cb_interface)
            cb_interface->onConnected(this);
        
        return true;
    }

    void Socket::poll() {
        receive();
    }

    size_t Socket::available() {
        return in_buffer.length();
    }

    Memory Socket::read(int max) {
        Memory ret;
        if (in_buffer.length() && recv_lock.isLockedByMe()) {
            int len = in_buffer.length() < max ? (int)in_buffer.length() : max;
            Memory bytes = in_buffer.fetch(len);
            ret = Memory(bytes.getPtr(), len);
        }
        return ret;
    }

    int Socket::send(Memory data) {
        return send(data.operator char *(), data.length());
    }
    
    int Socket::send(const char* buffer, size_t len) {
        size_t space = 0, add = 0;
        
        Mutex::Lock lock(send_lock);
        if (event_write) {
            space = out_buffer.freeSpace();
            if (space && event_write) {
                add = space < len ? space : len;
                add = out_buffer.append(buffer, add);
                event_add(event_write, NULL);
            }
        }

        return (int)add;
    }
    
    void Socket::setCallbackInterface(CallbackInterface *cb) {
        this->cb_interface = cb;
    }
    
    size_t Socket::writeBufferSpace() {
        return out_buffer.freeSpace();
    }

    void Socket::init(EventBase *event_base) {
        Socket::event_base = event_base;
    }

    Socket::ReceiveTask::ReceiveTask(Socket *socket) {
        this->socket = socket;
        this->self = elara::threading::memory::Ref<Task>(this);
    }

    Socket::ReceiveTask::~ReceiveTask() {
        this->self.release();
    }

    void Socket::ReceiveTask::run() {
        Mutex::Lock lock(socket->recv_lock);
        try {
            while (socket->in_buffer.length()) {
                socket->onReceive();
            }
        } catch (...) {
            printf("RT: Error\r\n");
        }
        finished();
    }
    
    void Socket::enableEvents() {
        event_write = event_new(event_base->getEventBase(), fd, EV_WRITE, ev_write, (void*)this);
        event_read = event_new(event_base->getEventBase(), fd, EV_READ|EV_PERSIST, ev_read, (void*)this);
        event_add(event_read, NULL);
        event_add(event_write, NULL);
    }

    void Socket::receive() {
        if (recv_lock.tryLock("receive")) {
            size_t fs = in_buffer.freeSpace();
            if (fs > 0) {
                char buf[fs];
                
                ssize_t s = ::recv(fd, buf, fs, 0);
                if (s == 0) {
                    close();
                } else if (s == -1 && errno !=EAGAIN) {
                    close();
                } else if (s != -1) {
                    fs -= in_buffer.append(buf, s);
                }
            }
            
            recv_lock.release();
            Thread::runTask(recv_task.self);
        }
    }

    void Socket::sendReady() {
        try {
            if (out_buffer.length()) {
                Mutex::Lock lock(send_lock);
                if (out_buffer.length()) {
                    int flags = 0;
                    Memory data = out_buffer.getDataUntilEnd();
                    int s = (int)::send(fd, data.operator char *(), (ssize_t)data.length(), flags);
                    if (!s || (s==-1 && errno!=EAGAIN)) {
                        close();
                    } else if (s) {
                        out_buffer.drop(s);
                    }
                    
                    if (s >= 0 && out_buffer.length())
                        event_add(event_write, NULL);
                }
            }
        } catch (...) {
            printf("err\r\n");
        }
        
        onWriteReady();
    }

    void Socket::close() {
        bool recv_locked = false, send_locked = false;
        
        if (!recv_lock.isLocked() || !recv_lock.isLockedByMe()) {
            recv_lock.lock();
            recv_locked = true;
        }
        
        if (!send_lock.isLocked() || !send_lock.isLockedByMe()) {
            send_lock.lock();
            send_locked = true;
        }

        if (fd)
            ::close(fd);
        
        if (event_read) {
            event_del(event_read);
            event_free(event_read);
            event_read = 0;
        }
        
        if (event_write) {
            event_del(event_write);
            event_free(event_write);
            event_write = 0;
        }
        
        fd = 0;
        
        if (recv_locked)
            recv_lock.release();
        
        if (send_locked)
            send_lock.release();
        
        if (cb_interface)
            cb_interface->onClosed(this);
    }

    void Socket::ev_read(int fd, short ev, void *arg) {
        ((Socket*)arg)->receive();
    }

    void Socket::ev_write(int fd, short ev, void *arg) {
        ((Socket*)arg)->sendReady();
    }
    
}

#ifndef %SocketServerName%_h
#define %SocketServerName%_h

#include <libelarasockets/Listener.h>

#include <libelaracore/memory/String.h>

class %SocketServerName% : public elara::Listener {
public:
    %SocketServerName%();
    virtual ~%SocketServerName%();
    void start(int port, elara::String address);

protected:
    virtual void onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
};

#endif

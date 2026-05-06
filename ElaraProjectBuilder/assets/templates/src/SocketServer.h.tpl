>>>>>>>>>>main>>>>SOCKET_SERVER_NAME
#ifndef %SOCKET_SERVER_NAME%_h
#define %SOCKET_SERVER_NAME%_h

#include <libelarasockets/Listener.h>

#include <libelaracore/memory/String.h>

class %SOCKET_SERVER_NAME% : public elara::Listener {
public:
    %SOCKET_SERVER_NAME%();
    virtual ~%SOCKET_SERVER_NAME%();
    void start(int port, elara::String address);

protected:
    virtual void onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
};

#endif
<<<<<<<<<<main

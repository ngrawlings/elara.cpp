>>>>>>>>>>main>>>>SOCKET_CLIENT_NAME
#ifndef %SOCKET_CLIENT_NAME%_h
#define %SOCKET_CLIENT_NAME%_h

#include <libelarasockets/Socket.h>
#include <libelaracore/memory/String.h>

class %SOCKET_CLIENT_NAME% : public elara::Socket {
public:
    %SOCKET_CLIENT_NAME%(elara::String address, int port);
    virtual ~%SOCKET_CLIENT_NAME%();
    void sendLine(elara::String text);

protected:
    virtual void onReceive();
    virtual void onWriteReady();
};

#endif
<<<<<<<<<<main

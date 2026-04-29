#ifndef %SocketClientName%_h
#define %SocketClientName%_h

#include <libelarasockets/Socket.h>
#include <libelaracore/memory/String.h>

class %SocketClientName% : public elara::Socket {
public:
    %SocketClientName%(elara::String address, int port);
    virtual ~%SocketClientName%();
    void sendLine(elara::String text);

protected:
    virtual void onReceive();
    virtual void onWriteReady();
};

#endif

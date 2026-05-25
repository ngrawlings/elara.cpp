#ifndef EPADBGSERVER_H
#define EPADBGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include <libelaraevent/EventBase.h>
#include "EpaDbgService.h"

namespace elara {

class EpaDbgServer : public sockets::rpc::json::JsonRPCServer {
public:
    EpaDbgServer();
    virtual ~EpaDbgServer();
    void start(int port, const String &address, EventBase *event_base);

private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};

} // namespace elara

#endif

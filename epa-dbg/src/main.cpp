#include <stdlib.h>
#include <stdio.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelaraevent/EventBase.h>
#include <libelarasockets/Socket.h>

#include "EpaDbgServer.h"

using namespace elara;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [port] [address]\n", argv0);
    fprintf(stderr, "  port     TCP port to listen on (default: 18878)\n");
    fprintf(stderr, "  address  Bind address (default: 127.0.0.1)\n");
}

int main(int argc, const char *argv[]) {
    int port = 18878;
    String address("127.0.0.1");

    if (argc > 1) {
        if (argv[1][0] == '-') { usage(argv[0]); return 1; }
        port = atoi(argv[1]);
    }
    if (argc > 2) address = String(argv[2]);

    Task::staticInit();
    Thread::init(4);

    EventBase *ev = new EventBase();
    Socket::init(ev);

    printf("epa-dbg: listening on %s:%d\n", address.operator char *(), port);
    fflush(stdout);

    EpaDbgServer server;
    server.start(port, address, ev);

    Thread::stopAllThreads();
    Thread::staticCleanUp();
    Task::staticCleanup();
    delete ev;
    return 0;
}

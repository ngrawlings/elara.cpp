#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelaraevent/EventBase.h>
#include <libelarasockets/Socket.h>

#include "EpaDbgServer.h"

using namespace elara;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--debug] [port] [address]\n", argv0);
    fprintf(stderr, "  --debug  Run the CPU debug VM service (currently the default)\n");
    fprintf(stderr, "  port     TCP port to listen on (default: 18878)\n");
    fprintf(stderr, "  address  Bind address (default: 127.0.0.1)\n");
}

int main(int argc, const char *argv[]) {
    int port = 18878;
    String address("127.0.0.1");
    int argi = 1;

    if (argc > argi && strcmp(argv[argi], "--debug") == 0) {
        argi++;
    }
    if (argc > argi) {
        if (argv[argi][0] == '-') { usage(argv[0]); return 1; }
        port = atoi(argv[argi]);
        argi++;
    }
    if (argc > argi) {
        address = String(argv[argi]);
        argi++;
    }
    if (argc > argi) { usage(argv[0]); return 1; }

    Task::staticInit();
    Thread::init(4);

    EventBase *ev = new EventBase();
    Socket::init(ev);

    printf("epavm: debug service listening on %s:%d\n", address.operator char *(), port);
    fflush(stdout);

    EpaDbgServer server;
    server.start(port, address, ev);

    Thread::stopAllThreads();
    Thread::staticCleanUp();
    Task::staticCleanup();
    delete ev;
    return 0;
}

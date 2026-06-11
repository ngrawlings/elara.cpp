#include <stdlib.h>
#include <stdio.h>
#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include "ElaraOsEpaDebugServer.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    String address("127.0.0.1");
    int port = 18878;
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) address = String(argv[2]);

    Task::staticInit();
    Thread::init(4);

    printf("Starting EPA debug RPC on %s:%d\n", address.operator char *(), port);
    ElaraOsEpaDebugServer server;
    server.start(port, address);

    Thread::stopAllThreads();
    Thread::staticCleanUp();
    Task::staticCleanup();
    return 0;
}

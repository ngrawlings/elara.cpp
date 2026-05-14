>>>>>>>>>>main>>>>SERVER_NAME
#include <stdlib.h>
#include <stdio.h>
#include "%SERVER_NAME%.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    String address("127.0.0.1");
    int port = 18878;
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) address = String(argv[2]);
    printf("Starting EPA debug RPC on %s:%d\n", address.operator char *(), port);
    %SERVER_NAME% server;
    server.start(port, address);
    return 0;
}
<<<<<<<<<<main

>>>>>>>>>>main>>>>CLASS_NAME>SOCKET_ADDRESS>SOCKET_PORT
#include <stdlib.h>
#include "%CLASS_NAME%.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    String host("%SOCKET_ADDRESS%");
    int port = %SOCKET_PORT%;
    if (argc > 1) host = String(argv[1]);
    if (argc > 2) port = atoi(argv[2]);
    %CLASS_NAME% app(host, port);
    return app.run();
}
<<<<<<<<<<main

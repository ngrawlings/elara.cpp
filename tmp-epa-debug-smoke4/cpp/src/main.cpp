#include <stdlib.h>
#include "EpaDebugSmokeApp.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    String host("127.0.0.1");
    int port = 18777;
    if (argc > 1) host = String(argv[1]);
    if (argc > 2) port = atoi(argv[2]);
    EpaDebugSmokeApp app(host, port);
    return app.run();
}

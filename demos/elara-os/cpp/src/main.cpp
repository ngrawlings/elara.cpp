#include <stdlib.h>
#include "ElaraOsApp.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    String host("127.0.0.1");
    int port = 18820;
    String host_bridge_host("127.0.0.1");
    int host_bridge_port = 0;
    if (argc > 1) host = String(argv[1]);
    if (argc > 2) port = atoi(argv[2]);
    const char *bridge_host_env = getenv("ELARA_IDE_HOST_BRIDGE_HOST");
    const char *bridge_port_env = getenv("ELARA_IDE_HOST_BRIDGE_PORT");
    if (bridge_host_env && bridge_host_env[0]) {
        host_bridge_host = String(bridge_host_env);
    }
    if (bridge_port_env && bridge_port_env[0]) {
        host_bridge_port = atoi(bridge_port_env);
    }
    ElaraOsApp app(host, port, host_bridge_host, host_bridge_port);
    return app.run();
}

#include <stdio.h>
#include <string.h>
#include <libelaracore/memory/String.h>

using namespace elara;

static void printHelp() {
    printf("Commands:\n");
    printf("  help  - show this help\n");
    printf("  quit  - exit the application\n");
}


int main() {


    if (1 == 1) {
        printHelp();
        char line[1024];
        while (true) {
            printf("elara-repl-client> ");
            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }
            String command = String(line);
            command = command.trim();
            if (!command.length()) {
                continue;
            }
            if (command == String("help")) {
                printHelp();
                continue;
            }
            if (command == String("quit") || command == String("exit")) {
                break;
            }
            printf("Unhandled command: %s\n", command.operator char *());
        }
    } else if (0 == 1) {
    } else if (0 == 1) {
    } else {
    printf("ElaraReplClient generated successfully. Add your application logic here.\n");
    }

    return 0;
}

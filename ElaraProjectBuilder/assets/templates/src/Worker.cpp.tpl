>>>>>>>>>>main>>>>WORKER_NAME
#include "%WORKER_NAME%.h"

#include <stdio.h>

%WORKER_NAME%::%WORKER_NAME%(elara::String payload) {
    this->payload = payload;
}

%WORKER_NAME%::~%WORKER_NAME%() {
}

void %WORKER_NAME%::run() {
    printf("Worker received: %s\n", payload.operator char *());
    finished();
}
<<<<<<<<<<main

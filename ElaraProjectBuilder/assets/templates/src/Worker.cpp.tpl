#include "%WorkerName%.h"

#include <stdio.h>

%WorkerName%::%WorkerName%(elara::String payload) {
    this->payload = payload;
}

%WorkerName%::~%WorkerName%() {
}

void %WorkerName%::run() {
    printf("Worker received: %s\n", payload.operator char *());
    finished();
}

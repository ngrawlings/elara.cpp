#include "PythonThreadRuntime.h"

#include <pthread.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>

namespace elara {
namespace pythonthreads {

static pthread_mutex_t g_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_runtime_initialized = false;

void PythonThreadRuntime::ensureInitialized(int thread_count) {
    if (thread_count < 1)
        thread_count = 1;

    pthread_mutex_lock(&g_runtime_mutex);

    if (!g_runtime_initialized) {
        Task::staticInit();
        Thread::init(thread_count);
        g_runtime_initialized = true;
    }

    pthread_mutex_unlock(&g_runtime_mutex);
}

void PythonThreadRuntime::shutdown() {
    pthread_mutex_lock(&g_runtime_mutex);

    if (g_runtime_initialized) {
        Thread::stopAllThreads();
        Thread::staticCleanUp();
        Task::staticCleanup();
        g_runtime_initialized = false;
    }

    pthread_mutex_unlock(&g_runtime_mutex);
}

void PythonThreadRuntime::getPoolState(int *total, int *active) {
    pthread_mutex_lock(&g_runtime_mutex);

    if (!g_runtime_initialized) {
        if (total)
            *total = 0;
        if (active)
            *active = 0;
    } else {
        Thread::getThreadPoolState(total, active);
    }

    pthread_mutex_unlock(&g_runtime_mutex);
}

}
}

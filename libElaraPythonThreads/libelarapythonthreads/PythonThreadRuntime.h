#ifndef __libElaraPythonThreads__PythonThreadRuntime__
#define __libElaraPythonThreads__PythonThreadRuntime__

namespace elara {
namespace pythonthreads {

class PythonThreadRuntime {
public:
    static void ensureInitialized(int thread_count=1);
    static void shutdown();
    static void getPoolState(int *total, int *active);
};

}
}

#endif

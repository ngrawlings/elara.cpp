#ifndef __libElaraPythonThreads__PythonProcessTask__
#define __libElaraPythonThreads__PythonProcessTask__

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>
#include <libelarathreads/Mutex.h>
#include <libelarathreads/Task.h>

namespace elara {
namespace pythonthreads {

struct PythonProcessSnapshot {
    bool started;
    bool queued;
    bool running;
    bool finished;
    bool stop_requested;
    bool spawn_failed;
    long pid;
    int exit_code;
    int term_signal;
    String python_executable;
    String script_path;
    String command;
    String stdout_text;
    String stderr_text;
    String error_text;

    PythonProcessSnapshot();
};

class PythonProcessTask : public Task {
public:
    PythonProcessTask(const String &python_executable, const String &script_path, const Array<String> &arguments);
    virtual ~PythonProcessTask();

    bool markQueued();
    void requestStop();
    bool waitForCompletion(long timeout_ms=-1);
    void getSnapshot(PythonProcessSnapshot *snapshot);

protected:
    virtual void run();

private:
    Array<String> arguments;
    Mutex state_mutex;
    PythonProcessSnapshot state;

    void setError(const String &error_text, bool spawn_failed=false);
    void appendStdout(const String &text);
    void appendStderr(const String &text);
    bool shouldStop();
};

}
}

#endif

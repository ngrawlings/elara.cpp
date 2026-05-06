#include "PythonProcessTask.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace elara {
namespace pythonthreads {

namespace {

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void closeIfOpen(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool drainPipe(int *fd, String *target) {
    char buffer[4096];

    while (*fd >= 0) {
        ssize_t read_length = read(*fd, buffer, sizeof(buffer));
        if (read_length > 0) {
            target->append(String(buffer, (size_t)read_length));
            continue;
        }

        if (read_length == 0) {
            closeIfOpen(fd);
            return false;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;

        closeIfOpen(fd);
        return false;
    }

    return false;
}

String buildCommandString(const String &python_executable, const String &script_path, const Array<String> &arguments) {
    String command = python_executable;
    command += " ";
    command += script_path;

    for (size_t i=0; i<arguments.length(); i++) {
        command += " ";
        command += arguments.get((unsigned int)i);
    }

    return command;
}

}

PythonProcessSnapshot::PythonProcessSnapshot() :
    started(false),
    queued(false),
    running(false),
    finished(false),
    stop_requested(false),
    spawn_failed(false),
    pid(0),
    exit_code(-1),
    term_signal(0) {
}

PythonProcessTask::PythonProcessTask(const String &python_executable, const String &script_path, const Array<String> &arguments) :
    arguments(arguments),
    state_mutex("PythonProcessTask::state_mutex", false) {
    state.python_executable = python_executable;
    state.script_path = script_path;
    state.command = buildCommandString(python_executable, script_path, arguments);
}

PythonProcessTask::~PythonProcessTask() {
    requestStop();
}

bool PythonProcessTask::markQueued() {
    Mutex::Lock lock(state_mutex);

    if (state.queued || state.running || state.finished)
        return false;

    state.started = true;
    state.queued = true;
    state.error_text = String();
    return true;
}

void PythonProcessTask::requestStop() {
    long pid = 0;

    {
        Mutex::Lock lock(state_mutex);
        state.stop_requested = true;
        pid = state.pid;
    }

    if (pid > 0)
        kill((pid_t)pid, SIGTERM);
}

bool PythonProcessTask::waitForCompletion(long timeout_ms) {
    long waited_ms = 0;

    while (true) {
        {
            Mutex::Lock lock(state_mutex);
            if (state.finished)
                return true;
        }

        if (timeout_ms >= 0 && waited_ms >= timeout_ms)
            return false;

        usleep(10000);
        waited_ms += 10;
    }
}

void PythonProcessTask::getSnapshot(PythonProcessSnapshot *snapshot) {
    if (!snapshot)
        return;

    Mutex::Lock lock(state_mutex);
    *snapshot = state;
}

void PythonProcessTask::setError(const String &error_text, bool spawn_failed) {
    Mutex::Lock lock(state_mutex);
    state.error_text = error_text;
    state.spawn_failed = spawn_failed;
}

void PythonProcessTask::appendStdout(const String &text) {
    Mutex::Lock lock(state_mutex);
    state.stdout_text.append(text);
}

void PythonProcessTask::appendStderr(const String &text) {
    Mutex::Lock lock(state_mutex);
    state.stderr_text.append(text);
}

bool PythonProcessTask::shouldStop() {
    Mutex::Lock lock(state_mutex);
    return state.stop_requested;
}

void PythonProcessTask::run() {
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };
    pid_t child_pid = -1;
    bool child_exited = false;
    bool stop_signal_sent = false;

    {
        Mutex::Lock lock(state_mutex);
        state.queued = false;
        state.running = true;
        state.finished = false;
        state.spawn_failed = false;
        state.exit_code = -1;
        state.term_signal = 0;
        state.pid = 0;
        state.stdout_text = String();
        state.stderr_text = String();
        state.error_text = String();
    }

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        setError(String("pipe failed errno=").arg((long long)errno), true);
        closeIfOpen(&stdout_pipe[0]);
        closeIfOpen(&stdout_pipe[1]);
        closeIfOpen(&stderr_pipe[0]);
        closeIfOpen(&stderr_pipe[1]);
    } else {
        child_pid = fork();

        if (child_pid == 0) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);

            closeIfOpen(&stdout_pipe[0]);
            closeIfOpen(&stdout_pipe[1]);
            closeIfOpen(&stderr_pipe[0]);
            closeIfOpen(&stderr_pipe[1]);

            size_t argc = arguments.length() + 3;
            char **argv = new char*[argc];
            argv[0] = const_cast<char *>((const char *)state.python_executable);
            argv[1] = const_cast<char *>((const char *)state.script_path);
            for (size_t i=0; i<arguments.length(); i++)
                argv[i + 2] = const_cast<char *>((const char *)arguments.get((unsigned int)i));
            argv[argc - 1] = 0;

            execvp(argv[0], argv);

            fprintf(stderr, "execvp failed errno=%d\n", errno);
            _exit(127);
        }

        closeIfOpen(&stdout_pipe[1]);
        closeIfOpen(&stderr_pipe[1]);
        setNonBlocking(stdout_pipe[0]);
        setNonBlocking(stderr_pipe[0]);

        if (child_pid < 0) {
            setError(String("fork failed errno=").arg((long long)errno), true);
        } else {
            {
                Mutex::Lock lock(state_mutex);
                state.pid = (long)child_pid;
            }

            while (!child_exited || stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
                struct pollfd poll_fds[2];
                nfds_t poll_count = 0;

                if (stdout_pipe[0] >= 0) {
                    poll_fds[poll_count].fd = stdout_pipe[0];
                    poll_fds[poll_count].events = POLLIN | POLLHUP | POLLERR;
                    poll_fds[poll_count].revents = 0;
                    poll_count++;
                }

                if (stderr_pipe[0] >= 0) {
                    poll_fds[poll_count].fd = stderr_pipe[0];
                    poll_fds[poll_count].events = POLLIN | POLLHUP | POLLERR;
                    poll_fds[poll_count].revents = 0;
                    poll_count++;
                }

                if (poll_count > 0)
                    poll(poll_fds, poll_count, 50);
                else
                    usleep(50000);

                if (stdout_pipe[0] >= 0) {
                    String stdout_chunk;
                    drainPipe(&stdout_pipe[0], &stdout_chunk);
                    if (stdout_chunk.length())
                        appendStdout(stdout_chunk);
                }

                if (stderr_pipe[0] >= 0) {
                    String stderr_chunk;
                    drainPipe(&stderr_pipe[0], &stderr_chunk);
                    if (stderr_chunk.length())
                        appendStderr(stderr_chunk);
                }

                if (!child_exited) {
                    int status = 0;
                    pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
                    if (wait_result == child_pid) {
                        Mutex::Lock lock(state_mutex);
                        child_exited = true;
                        state.pid = 0;
                        state.running = false;

                        if (WIFEXITED(status))
                            state.exit_code = WEXITSTATUS(status);
                        else if (WIFSIGNALED(status))
                            state.term_signal = WTERMSIG(status);
                    } else if (wait_result < 0) {
                        child_exited = true;
                        setError(String("waitpid failed errno=").arg((long long)errno));
                    }
                }

                if (!stop_signal_sent && shouldStop() && child_pid > 0 && !child_exited) {
                    kill(child_pid, SIGTERM);
                    stop_signal_sent = true;
                }
            }
        }
    }

    closeIfOpen(&stdout_pipe[0]);
    closeIfOpen(&stdout_pipe[1]);
    closeIfOpen(&stderr_pipe[0]);
    closeIfOpen(&stderr_pipe[1]);

    {
        Mutex::Lock lock(state_mutex);
        state.running = false;
        state.queued = false;
        state.finished = true;
        state.pid = 0;
    }

    finished();
}

}
}

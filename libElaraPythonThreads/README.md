# libElaraPythonThreads

`libElaraPythonThreads` exposes a small piece of the Elara native threading system to Python.

Current surface:
- `init_pool(thread_count=1)`
- `shutdown_pool()`
- `pool_state()`
- `PythonProcessThread(script_path, args=None, python_executable=None)`

`PythonProcessThread` queues a native `Task` onto `libElaraThreads`, starts a child Python process, captures stdout/stderr, and exposes a stable snapshot back to Python.

In-tree demo:

```bash
cd /home/nyhl/workspace/elara.cpp
python3 libElaraPythonThreads/demo/example.py
```

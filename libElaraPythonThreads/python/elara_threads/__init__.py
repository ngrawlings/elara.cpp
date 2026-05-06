import sys

from ._native import _NativePythonProcessThread
from ._native import init_pool
from ._native import pool_state
from ._native import shutdown_pool


class PythonProcessThread:
    def __init__(self, script_path, args=None, python_executable=None):
        if python_executable is None:
            python_executable = sys.executable
        if args is None:
            args = []
        self._native = _NativePythonProcessThread(python_executable, script_path, list(args))

    def start(self):
        self._native.start()
        return self

    def stop(self):
        self._native.stop()

    def wait(self, timeout_ms=-1):
        return self._native.wait(timeout_ms=timeout_ms)

    def snapshot(self):
        return self._native.snapshot()


__all__ = [
    "PythonProcessThread",
    "init_pool",
    "pool_state",
    "shutdown_pool",
]

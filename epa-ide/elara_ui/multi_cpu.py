try:
    from elara_threads import PythonProcessThread
    from elara_threads import init_pool
    _ELARA_THREADS_IMPORT_ERROR = None
except ModuleNotFoundError as exc:
    PythonProcessThread = None
    init_pool = None
    _ELARA_THREADS_IMPORT_ERROR = exc


def _require_elara_threads():
    if _ELARA_THREADS_IMPORT_ERROR is not None:
        raise RuntimeError(
            "Optional Python multi-core support requires the installed elara_threads package"
        ) from _ELARA_THREADS_IMPORT_ERROR


def ensure_multi_cpu_runtime(thread_count=2):
    _require_elara_threads()
    init_pool(thread_count)


class MultiCpuWorkerTemplate:
    def __init__(self, script_path, args=None, python_executable=None):
        _require_elara_threads()
        self._thread = PythonProcessThread(script_path, args=args or [], python_executable=python_executable)

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._thread.stop()

    def wait(self, timeout_ms=-1):
        return self._thread.wait(timeout_ms=timeout_ms)

    def snapshot(self):
        return self._thread.snapshot()

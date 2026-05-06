#!/usr/bin/env python3

import pathlib
import pprint
import sys


def main():
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    python_version = f"python{sys.version_info.major}.{sys.version_info.minor}"
    site_packages = repo_root / "build" / "lib" / python_version / "site-packages"
    sys.path.insert(0, str(site_packages))

    from elara_threads import PythonProcessThread
    from elara_threads import init_pool
    from elara_threads import pool_state

    init_pool(thread_count=2)

    worker_path = pathlib.Path(__file__).resolve().with_name("worker_template.py")
    process_thread = PythonProcessThread(str(worker_path), args=["6"])
    process_thread.start()
    process_thread.wait()

    pprint.pprint(pool_state())
    pprint.pprint(process_thread.snapshot())


if __name__ == "__main__":
    main()

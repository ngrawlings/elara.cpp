#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <libelarapythonthreads/PythonProcessTask.h>
#include <libelarapythonthreads/PythonThreadRuntime.h>
#include <libelarathreads/Thread.h>

using elara::Array;
using elara::String;
using elara::Task;
using elara::pythonthreads::PythonProcessSnapshot;
using elara::pythonthreads::PythonProcessTask;
using elara::pythonthreads::PythonThreadRuntime;
using elara::threading::memory::Ref;

typedef struct {
    PyObject_HEAD
    Ref<Task> *task_ref;
} NativePythonProcessThreadObject;

static int setDictItemOwned(PyObject *dict, const char *key, PyObject *value) {
    if (!value)
        return -1;

    int rc = PyDict_SetItemString(dict, key, value);
    Py_DECREF(value);
    return rc;
}

static PyObject *stringToPyUnicode(const String &text) {
    const char *ptr = (const char *)text;
    if (!ptr)
        ptr = "";
    return PyUnicode_FromString(ptr);
}

static PythonProcessTask *getTaskImpl(NativePythonProcessThreadObject *self) {
    if (!self->task_ref)
        return 0;

    return dynamic_cast<PythonProcessTask *>(self->task_ref->getPtr());
}

static PyObject *NativePythonProcessThread_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    NativePythonProcessThreadObject *self = (NativePythonProcessThreadObject *)type->tp_alloc(type, 0);
    if (self)
        self->task_ref = 0;
    return (PyObject *)self;
}

static int NativePythonProcessThread_init(NativePythonProcessThreadObject *self, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = { (char *)"python_executable", (char *)"script_path", (char *)"args", 0 };
    const char *python_executable = 0;
    const char *script_path = 0;
    PyObject *arg_sequence = Py_None;
    Array<String> native_args;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|O", kwlist, &python_executable, &script_path, &arg_sequence))
        return -1;

    if (arg_sequence != Py_None) {
        if (!PySequence_Check(arg_sequence)) {
            PyErr_SetString(PyExc_TypeError, "args must be a sequence of strings");
            return -1;
        }

        Py_ssize_t arg_count = PySequence_Size(arg_sequence);
        for (Py_ssize_t i=0; i<arg_count; i++) {
            PyObject *item = PySequence_GetItem(arg_sequence, i);
            if (!item)
                return -1;

            if (!PyUnicode_Check(item)) {
                Py_DECREF(item);
                PyErr_SetString(PyExc_TypeError, "args entries must be strings");
                return -1;
            }

            const char *value = PyUnicode_AsUTF8(item);
            if (!value) {
                Py_DECREF(item);
                return -1;
            }

            native_args.push(String(value));
            Py_DECREF(item);
        }
    }

    delete self->task_ref;
    self->task_ref = new Ref<Task>(new PythonProcessTask(String(python_executable), String(script_path), native_args));
    return 0;
}

static void NativePythonProcessThread_dealloc(NativePythonProcessThreadObject *self) {
    delete self->task_ref;
    self->task_ref = 0;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *NativePythonProcessThread_start(NativePythonProcessThreadObject *self, PyObject *) {
    PythonProcessTask *task = getTaskImpl(self);
    if (!task) {
        PyErr_SetString(PyExc_RuntimeError, "task is not initialized");
        return 0;
    }

    if (!task->markQueued()) {
        PyErr_SetString(PyExc_RuntimeError, "task has already been started");
        return 0;
    }

    PythonThreadRuntime::ensureInitialized(1);
    elara::Thread::runTask(*self->task_ref);
    Py_RETURN_NONE;
}

static PyObject *NativePythonProcessThread_stop(NativePythonProcessThreadObject *self, PyObject *) {
    PythonProcessTask *task = getTaskImpl(self);
    if (!task) {
        PyErr_SetString(PyExc_RuntimeError, "task is not initialized");
        return 0;
    }

    task->requestStop();
    Py_RETURN_NONE;
}

static PyObject *NativePythonProcessThread_wait(NativePythonProcessThreadObject *self, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = { (char *)"timeout_ms", 0 };
    long timeout_ms = -1;
    PythonProcessTask *task = getTaskImpl(self);

    if (!task) {
        PyErr_SetString(PyExc_RuntimeError, "task is not initialized");
        return 0;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|l", kwlist, &timeout_ms))
        return 0;

    bool completed = false;
    Py_BEGIN_ALLOW_THREADS
    completed = task->waitForCompletion(timeout_ms);
    Py_END_ALLOW_THREADS

    if (completed)
        Py_RETURN_TRUE;

    Py_RETURN_FALSE;
}

static PyObject *NativePythonProcessThread_snapshot(NativePythonProcessThreadObject *self, PyObject *) {
    PythonProcessTask *task = getTaskImpl(self);
    PythonProcessSnapshot snapshot;

    if (!task) {
        PyErr_SetString(PyExc_RuntimeError, "task is not initialized");
        return 0;
    }

    task->getSnapshot(&snapshot);

    PyObject *dict = PyDict_New();
    if (!dict)
        return 0;

    if (setDictItemOwned(dict, "started", PyBool_FromLong(snapshot.started ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "queued", PyBool_FromLong(snapshot.queued ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "running", PyBool_FromLong(snapshot.running ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "finished", PyBool_FromLong(snapshot.finished ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "stop_requested", PyBool_FromLong(snapshot.stop_requested ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "spawn_failed", PyBool_FromLong(snapshot.spawn_failed ? 1 : 0)) != 0 ||
        setDictItemOwned(dict, "pid", PyLong_FromLong(snapshot.pid)) != 0 ||
        setDictItemOwned(dict, "exit_code", PyLong_FromLong(snapshot.exit_code)) != 0 ||
        setDictItemOwned(dict, "term_signal", PyLong_FromLong(snapshot.term_signal)) != 0 ||
        setDictItemOwned(dict, "python_executable", stringToPyUnicode(snapshot.python_executable)) != 0 ||
        setDictItemOwned(dict, "script_path", stringToPyUnicode(snapshot.script_path)) != 0 ||
        setDictItemOwned(dict, "command", stringToPyUnicode(snapshot.command)) != 0 ||
        setDictItemOwned(dict, "stdout_text", stringToPyUnicode(snapshot.stdout_text)) != 0 ||
        setDictItemOwned(dict, "stderr_text", stringToPyUnicode(snapshot.stderr_text)) != 0 ||
        setDictItemOwned(dict, "error_text", stringToPyUnicode(snapshot.error_text)) != 0) {
        Py_DECREF(dict);
        return 0;
    }

    return dict;
}

static PyMethodDef NativePythonProcessThread_methods[] = {
    { "start", (PyCFunction)NativePythonProcessThread_start, METH_NOARGS, "Queue the process task on the Elara thread pool." },
    { "stop", (PyCFunction)NativePythonProcessThread_stop, METH_NOARGS, "Request shutdown of the child Python process." },
    { "wait", (PyCFunction)NativePythonProcessThread_wait, METH_VARARGS | METH_KEYWORDS, "Wait for process completion. Returns True if finished." },
    { "snapshot", (PyCFunction)NativePythonProcessThread_snapshot, METH_NOARGS, "Return a state snapshot for the managed Python process." },
    { 0, 0, 0, 0 }
};

static PyTypeObject NativePythonProcessThreadType = {
    PyVarObject_HEAD_INIT(0, 0)
};

static PyObject *module_init_pool(PyObject *, PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = { (char *)"thread_count", 0 };
    int thread_count = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwlist, &thread_count))
        return 0;

    PythonThreadRuntime::ensureInitialized(thread_count);
    Py_RETURN_NONE;
}

static PyObject *module_shutdown_pool(PyObject *, PyObject *) {
    PythonThreadRuntime::shutdown();
    Py_RETURN_NONE;
}

static PyObject *module_pool_state(PyObject *, PyObject *) {
    int total = 0;
    int active = 0;
    PythonThreadRuntime::getPoolState(&total, &active);

    PyObject *dict = PyDict_New();
    if (!dict)
        return 0;

    if (setDictItemOwned(dict, "total", PyLong_FromLong(total)) != 0 ||
        setDictItemOwned(dict, "active", PyLong_FromLong(active)) != 0) {
        Py_DECREF(dict);
        return 0;
    }
    return dict;
}

static PyMethodDef module_methods[] = {
    { "init_pool", (PyCFunction)module_init_pool, METH_VARARGS | METH_KEYWORDS, "Initialize the Elara thread pool." },
    { "shutdown_pool", (PyCFunction)module_shutdown_pool, METH_NOARGS, "Stop the Elara thread pool." },
    { "pool_state", (PyCFunction)module_pool_state, METH_NOARGS, "Return the Elara thread pool state." },
    { 0, 0, 0, 0 }
};

static struct PyModuleDef module_definition = {
    PyModuleDef_HEAD_INIT,
    "_native",
    "Native Elara thread bindings for Python process tasks.",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit__native(void) {
    NativePythonProcessThreadType.tp_name = "elara_threads._native._NativePythonProcessThread";
    NativePythonProcessThreadType.tp_basicsize = sizeof(NativePythonProcessThreadObject);
    NativePythonProcessThreadType.tp_flags = Py_TPFLAGS_DEFAULT;
    NativePythonProcessThreadType.tp_new = NativePythonProcessThread_new;
    NativePythonProcessThreadType.tp_init = (initproc)NativePythonProcessThread_init;
    NativePythonProcessThreadType.tp_dealloc = (destructor)NativePythonProcessThread_dealloc;
    NativePythonProcessThreadType.tp_methods = NativePythonProcessThread_methods;
    NativePythonProcessThreadType.tp_doc = "Native task wrapper that runs a child Python process on the Elara thread pool.";

    if (PyType_Ready(&NativePythonProcessThreadType) < 0)
        return 0;

    PyObject *module = PyModule_Create(&module_definition);
    if (!module)
        return 0;

    Py_INCREF(&NativePythonProcessThreadType);
    if (PyModule_AddObject(module, "_NativePythonProcessThread", (PyObject *)&NativePythonProcessThreadType) != 0) {
        Py_DECREF(&NativePythonProcessThreadType);
        Py_DECREF(module);
        return 0;
    }

    return module;
}

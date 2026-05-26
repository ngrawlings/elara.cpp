#include "EpaDbgVmHost.h"
#include <string.h>

namespace elara {

EpaDbgVmHost::EpaDbgVmHost() : kernel(NULL), module(NULL) {}

EpaDbgVmHost::~EpaDbgVmHost() { destroy(); }

void EpaDbgVmHost::setError(const char *prefix, const char *detail) {
    error_text = String(prefix ? prefix : "EPA VM host error");
    if (detail && detail[0]) {
        error_text += String(": ");
        error_text += String(detail);
    }
}

bool EpaDbgVmHost::create() {
    char err[EPA_MAX_ERR];
    err[0] = 0;
    destroy();
    kernel = epa_kernel_create(err);
    if (!kernel) { setError("epa_kernel_create failed", err); return false; }
    if (!epa_kernel_set_scheduler(kernel, EPA_SCHED_DEBUG, err)) {
        setError("epa_kernel_set_scheduler failed", err);
        epa_kernel_destroy(kernel);
        kernel = NULL;
        return false;
    }
    error_text = String();
    return true;
}

void EpaDbgVmHost::destroy() {
    if (module) { epa_kernel_module_destroy(module); module = NULL; }
    if (kernel) { epa_kernel_destroy(kernel); kernel = NULL; }
}

bool EpaDbgVmHost::setKernelId(const String &kernel_id) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) return false;
    err[0] = 0;
    String text(kernel_id);
    if (!epa_kernel_set_id(kernel, text.operator char *(), err)) {
        setError("epa_kernel_set_id failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::loadAsmPath(const String &asm_path) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) return false;
    err[0] = 0;
    String text(asm_path);
    if (!epa_kernel_load_asm(kernel, text.operator char *(), err)) {
        setError("epa_kernel_load_asm failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::loadBlob(const uint8_t *blob, size_t blob_len) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) return false;
    err[0] = 0;
    if (!blob || !blob_len) { setError("epa_kernel_load_blob failed", "empty blob"); return false; }
    if (!epa_kernel_load_blob(kernel, blob, blob_len, err)) {
        setError("epa_kernel_load_blob failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::loadBundlePath(const String &bundle_path) {
    char err[EPA_MAX_ERR];
    String text(bundle_path);
    err[0] = 0;
    destroy();
    module = epa_kernel_module_load_bundle(text.operator char *(), err);
    if (!module) { setError("epa_kernel_module_load_bundle failed", err); return false; }
    // Initialise worker structures so ingress queues and debug capture work.
    // Does NOT start execution — kernels stay paused until step/run is called.
    size_t count = epa_kernel_module_count(module);
    for (size_t i = 0; i < count; i++) {
        EpaKernel *k = epa_kernel_module_kernel(module, i);
        if (!k) continue;
        uint32_t n = epa_kernel_worker_count(k);
        if (n > 0) epa_kernel_module_add_kernel_threads(module, i, n, err);
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::ingressPush(uint32_t wid, const void *data, uint32_t len) {
    if (!kernel) { setError("ingressPush failed", "kernel not created"); return false; }
    if (!epa_kernel_ingress_push(kernel, wid, data, len)) {
        setError("epa_kernel_ingress_push failed", "queue full or invalid wid");
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    if (!kernel) { setError("ingressPushTagged failed", "kernel not created"); return false; }
    if (!epa_kernel_ingress_push_tagged(kernel, wid, tag, data, len)) {
        setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid wid");
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
    EpaKernel *target = rawKernelAt(index);
    if (!target) { setError("ingressPushToKernel failed", "kernel index out of range"); return false; }
    if (!epa_kernel_ingress_push(target, wid, data, len)) {
        setError("epa_kernel_ingress_push failed", "queue full or invalid wid");
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    EpaKernel *target = rawKernelAt(index);
    if (!target) { setError("ingressPushTaggedToKernel failed", "kernel index out of range"); return false; }
    if (!epa_kernel_ingress_push_tagged(target, wid, tag, data, len)) {
        setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid wid");
        return false;
    }
    error_text = String();
    return true;
}

EpaKernel *EpaDbgVmHost::rawKernel() const { return kernel; }

EpaKernel *EpaDbgVmHost::rawKernelAt(size_t index) const {
    return module ? epa_kernel_module_kernel(module, index) : NULL;
}

size_t EpaDbgVmHost::kernelCount() const {
    return module ? epa_kernel_module_count(module) : 0;
}

String EpaDbgVmHost::kernelPathId(size_t index) const {
    const char *id = module ? epa_kernel_module_path_id(module, index) : NULL;
    return id ? String(id) : String();
}

String EpaDbgVmHost::kernelStatus(size_t index) const {
    if (!module) return String();
    return String(epa_kernel_status_name(epa_kernel_module_kernel_status(module, index)));
}

String EpaDbgVmHost::kernelError(size_t index) const {
    const char *e = module ? epa_kernel_module_kernel_error(module, index) : NULL;
    return e ? String(e) : String();
}

uint32_t EpaDbgVmHost::kernelThreadCount(size_t index) const {
    return module ? epa_kernel_module_kernel_thread_count(module, index) : 0;
}

int EpaDbgVmHost::findKernelIndex(const String &path_id) const {
    if (!module) return -1;
    String text(path_id);
    return epa_kernel_module_find_kernel(module, text.operator char *());
}

bool EpaDbgVmHost::addKernelThreads(size_t index, uint32_t add_count) {
    char err[EPA_MAX_ERR];
    if (!module) { setError("addKernelThreads failed", "module not loaded"); return false; }
    err[0] = 0;
    if (!epa_kernel_module_add_kernel_threads(module, index, add_count, err)) {
        setError("epa_kernel_module_add_kernel_threads failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::startAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) { setError("startAllKernels failed", "module not loaded"); return false; }
    err[0] = 0;
    size_t count = epa_kernel_module_count(module);
    for (size_t i = 0; i < count; i++) {
        EpaKernel *k = epa_kernel_module_kernel(module, i);
        if (!k) continue;
        uint32_t n = epa_kernel_worker_count(k);
        if (n > 0) epa_kernel_module_add_kernel_threads(module, i, n, err);
    }
    if (!epa_kernel_module_start_all_kernels(module, err)) {
        setError("epa_kernel_module_start_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::stopAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) return true;
    err[0] = 0;
    if (!epa_kernel_module_stop_all_kernels(module, err)) {
        setError("epa_kernel_module_stop_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaDbgVmHost::isReady() const { return kernel != NULL || module != NULL; }

const String &EpaDbgVmHost::lastError() const { return error_text; }

} // namespace elara

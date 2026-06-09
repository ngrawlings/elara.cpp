#include "ElaraOsEpaVmHost.h"

#include <string.h>

namespace elara {

ElaraOsEpaVmHost::ElaraOsEpaVmHost()
    : kernel(NULL),
      module(NULL) {
}

ElaraOsEpaVmHost::~ElaraOsEpaVmHost() {
    destroy();
}

void ElaraOsEpaVmHost::setError(const char *prefix, const char *detail) {
    error_text = String(prefix ? prefix : "EPA VM host error");
    if (detail && detail[0]) {
        error_text += String(": ");
        error_text += String(detail);
    }
}

bool ElaraOsEpaVmHost::create() {
    char err[EPA_MAX_ERR];
    err[0] = 0;
    destroy();
    kernel = epa_kernel_create(err);
    if (!kernel) {
        setError("epa_kernel_create failed", err);
        return false;
    }
    if (!epa_kernel_set_scheduler(kernel, EPA_SCHED_CPU_THREAD, err)) {
        setError("epa_kernel_set_scheduler failed", err);
        epa_kernel_destroy(kernel);
        kernel = NULL;
        return false;
    }
    error_text = String();
    return true;
}

void ElaraOsEpaVmHost::destroy() {
    if (module) {
        epa_kernel_module_destroy(module);
        module = NULL;
    }
    if (kernel) {
        epa_kernel_destroy(kernel);
        kernel = NULL;
    }
}

bool ElaraOsEpaVmHost::setKernelId(const String &kernel_id) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) {
        return false;
    }
    err[0] = 0;
    String kernel_id_text(kernel_id);
    if (!epa_kernel_set_id(kernel, kernel_id_text.operator char *(), err)) {
        setError("epa_kernel_set_id failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::loadAsmPath(const String &asm_path) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) {
        return false;
    }
    err[0] = 0;
    String asm_path_text(asm_path);
    if (!epa_kernel_load_asm(kernel, asm_path_text.operator char *(), err)) {
        setError("epa_kernel_load_asm failed", err);
        return false;
    }
    uint32_t n = epa_kernel_worker_count(kernel);
    if (n > 0) {
        epa_kernel_add_threads(kernel, n, err);
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::loadBlob(const uint8_t *blob, size_t blob_len) {
    char err[EPA_MAX_ERR];
    if (!kernel && !create()) {
        return false;
    }
    err[0] = 0;
    if (!blob || !blob_len) {
        setError("epa_kernel_load_blob failed", "empty blob");
        return false;
    }
    if (!epa_kernel_load_blob(kernel, blob, blob_len, err)) {
        setError("epa_kernel_load_blob failed", err);
        return false;
    }
    uint32_t n = epa_kernel_worker_count(kernel);
    if (n > 0) {
        epa_kernel_add_threads(kernel, n, err);
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::loadBundlePath(const String &bundle_path) {
    char err[EPA_MAX_ERR];
    String bundle_text(bundle_path);
    err[0] = 0;
    destroy();
    module = epa_kernel_module_load_bundle(bundle_text.operator char *(), err);
    if (!module) {
        setError("epa_kernel_module_load_bundle failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::ingressPush(uint32_t wid, const void *data, uint32_t len) {
    if (!kernel) {
        setError("epa_kernel_ingress_push failed", "kernel not created");
        return false;
    }
    if (!epa_kernel_ingress_push(kernel, wid, data, len)) {
        setError("epa_kernel_ingress_push failed", "queue full or invalid worker id");
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    if (!kernel) {
        setError("epa_kernel_ingress_push_tagged failed", "kernel not created");
        return false;
    }
    if (!epa_kernel_ingress_push_tagged(kernel, wid, tag, data, len)) {
        setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid worker id");
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
    EpaKernel *target = rawKernelAt(index);
    if (!target) {
        setError("epa_kernel_ingress_push failed", "kernel index out of range");
        return false;
    }
    if (!epa_kernel_ingress_push(target, wid, data, len)) {
        setError("epa_kernel_ingress_push failed", "queue full or invalid worker id");
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    EpaKernel *target = rawKernelAt(index);
    if (!target) {
        setError("epa_kernel_ingress_push_tagged failed", "kernel index out of range");
        return false;
    }
    if (!epa_kernel_ingress_push_tagged(target, wid, tag, data, len)) {
        setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid worker id");
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::run(uint32_t max_ticks, bool debug) {
    char err[EPA_MAX_ERR];
    if (!kernel) {
        setError("epa_kernel_run failed", "kernel not created");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_run(kernel, max_ticks, debug ? 1 : 0, err)) {
        setError("epa_kernel_run failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::runKernelAt(size_t index, uint32_t max_ticks, bool debug) {
    char err[EPA_MAX_ERR];
    EpaKernel *target = rawKernelAt(index);
    if (!target) {
        setError("epa_kernel_run failed", "kernel index out of range");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_run(target, max_ticks, debug ? 1 : 0, err)) {
        setError("epa_kernel_run failed", err);
        return false;
    }
    error_text = String();
    return true;
}

void ElaraOsEpaVmHost::requestInterrupt() {
    if (kernel) {
        epa_kernel_request_interrupt(kernel);
    }
}

const uint8_t *ElaraOsEpaVmHost::resultData(size_t *out_len) const {
    if (out_len) *out_len = 0;
    error_text = String("epa_kernel_get_result unavailable: the current runtime bridge is module/status driven");
    return NULL;
}

EpaKernel *ElaraOsEpaVmHost::rawKernel() const {
    return kernel;
}

EpaKernel *ElaraOsEpaVmHost::rawKernelAt(size_t index) const {
    if (!module) {
        return NULL;
    }
    return epa_kernel_module_kernel(module, index);
}

size_t ElaraOsEpaVmHost::kernelCount() const {
    if (!module) {
        return 0;
    }
    return epa_kernel_module_count(module);
}

String ElaraOsEpaVmHost::kernelPathId(size_t index) const {
    const char *path_id = module ? epa_kernel_module_path_id(module, index) : NULL;
    return path_id ? String(path_id) : String();
}

String ElaraOsEpaVmHost::kernelStatus(size_t index) const {
    if (!module) {
        return String();
    }
    return String(epa_kernel_status_name(epa_kernel_module_kernel_status(module, index)));
}

String ElaraOsEpaVmHost::kernelError(size_t index) const {
    const char *error = module ? epa_kernel_module_kernel_error(module, index) : NULL;
    return error ? String(error) : String();
}

uint32_t ElaraOsEpaVmHost::kernelThreadCount(size_t index) const {
    if (!module) {
        return 0;
    }
    return epa_kernel_module_kernel_thread_count(module, index);
}

int ElaraOsEpaVmHost::findKernelIndex(const String &path_id) const {
    if (!module) {
        return -1;
    }
    String path_text(path_id);
    return epa_kernel_module_find_kernel(module, path_text.operator char *());
}

bool ElaraOsEpaVmHost::addKernelThreads(size_t index, uint32_t add_count) {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("epa_kernel_module_add_kernel_threads failed", "module not loaded");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_module_add_kernel_threads(module, index, add_count, err)) {
        setError("epa_kernel_module_add_kernel_threads failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::startAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("epa_kernel_module_start_all_kernels failed", "module not loaded");
        return false;
    }
    err[0] = 0;
    size_t count = epa_kernel_module_count(module);
    for (size_t i = 0; i < count; i++) {
        EpaKernel *k = epa_kernel_module_kernel(module, i);
        if (!k) continue;
        uint32_t n = epa_kernel_worker_count(k);
        if (n > 0) {
            epa_kernel_module_add_kernel_threads(module, i, n, err);
        }
    }
    if (!epa_kernel_module_start_all_kernels(module, err)) {
        setError("epa_kernel_module_start_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::stopAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) {
        return true;
    }
    err[0] = 0;
    if (!epa_kernel_module_stop_all_kernels(module, err)) {
        setError("epa_kernel_module_stop_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool ElaraOsEpaVmHost::isReady() const {
    return kernel != NULL || module != NULL;
}

const String &ElaraOsEpaVmHost::lastError() const {
    return error_text;
}

}

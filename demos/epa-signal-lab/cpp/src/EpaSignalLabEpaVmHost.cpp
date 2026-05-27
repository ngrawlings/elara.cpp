#include "EpaSignalLabEpaVmHost.h"

#include <string.h>

namespace elara {

EpaSignalLabEpaVmHost::EpaSignalLabEpaVmHost()
    : kernel(NULL),
      module(NULL) {
}

EpaSignalLabEpaVmHost::~EpaSignalLabEpaVmHost() {
    destroy();
}

void EpaSignalLabEpaVmHost::setError(const char *prefix, const char *detail) {
    error_text = String(prefix ? prefix : "EPA VM host error");
    if (detail && detail[0]) {
        error_text += String(": ");
        error_text += String(detail);
    }
}

bool EpaSignalLabEpaVmHost::create() {
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

void EpaSignalLabEpaVmHost::destroy() {
    if (module) {
        epa_kernel_module_destroy(module);
        module = NULL;
    }
    if (kernel) {
        epa_kernel_destroy(kernel);
        kernel = NULL;
    }
}

bool EpaSignalLabEpaVmHost::setKernelId(const String &kernel_id) {
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

bool EpaSignalLabEpaVmHost::loadAsmPath(const String &asm_path) {
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
    {
        uint32_t n = epa_kernel_worker_count(kernel);
        if (n > 0u) {
            epa_kernel_add_threads(kernel, n, err);
        }
    }
    error_text = String();
    return true;
}

bool EpaSignalLabEpaVmHost::loadBlob(const uint8_t *blob, size_t blob_len) {
    char err[EPA_MAX_ERR];
    if (!blob || !blob_len) {
        setError("epa_kernel_load_blob failed", "empty blob");
        return false;
    }
    if (!kernel && !create()) {
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_load_blob(kernel, blob, blob_len, err)) {
        setError("epa_kernel_load_blob failed", err);
        return false;
    }
    {
        uint32_t n = epa_kernel_worker_count(kernel);
        if (n > 0u) {
            epa_kernel_add_threads(kernel, n, err);
        }
    }
    error_text = String();
    return true;
}

bool EpaSignalLabEpaVmHost::loadBundlePath(const String &bundle_path) {
    char err[EPA_MAX_ERR];
    String bundle_path_text(bundle_path);
    destroy();
    err[0] = 0;
    module = epa_kernel_module_load_bundle(bundle_path_text.operator char *(), err);
    if (!module) {
        setError("epa_kernel_module_load_bundle failed", err);
        return false;
    }
    kernel = epa_kernel_module_count(module) > 0 ? epa_kernel_module_kernel(module, 0) : NULL;
    error_text = String();
    return true;
}

bool EpaSignalLabEpaVmHost::ingressPush(uint32_t wid, const void *data, uint32_t len) {
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

bool EpaSignalLabEpaVmHost::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
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

bool EpaSignalLabEpaVmHost::ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
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

bool EpaSignalLabEpaVmHost::ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
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

bool EpaSignalLabEpaVmHost::run(uint32_t max_ticks, bool debug) {
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

void EpaSignalLabEpaVmHost::requestInterrupt() {
    if (kernel) {
        epa_kernel_request_interrupt(kernel);
    }
}

size_t EpaSignalLabEpaVmHost::kernelCount() const {
    return module ? epa_kernel_module_count(module) : (kernel ? 1u : 0u);
}

String EpaSignalLabEpaVmHost::kernelPathId(size_t index) const {
    const char *path_id = NULL;
    if (module) {
        path_id = epa_kernel_module_path_id(module, index);
    } else if (kernel && index == 0u) {
        path_id = "kernel";
    }
    return path_id ? String(path_id) : String();
}

EpaKernelStatus EpaSignalLabEpaVmHost::kernelStatus(size_t index) const {
    if (module) {
        return epa_kernel_module_kernel_status(module, index);
    }
    if (kernel && index == 0u) {
        return epa_kernel_get_status(kernel);
    }
    return EPA_KERNEL_STATUS_ERROR;
}

String EpaSignalLabEpaVmHost::kernelStatusText(size_t index) const {
    return String(epa_kernel_status_name(kernelStatus(index)));
}

String EpaSignalLabEpaVmHost::kernelError(size_t index) const {
    const char *text = NULL;
    if (module) {
        text = epa_kernel_module_kernel_error(module, index);
    } else if (kernel && index == 0u) {
        text = epa_kernel_get_last_error(kernel);
    }
    return text ? String(text) : String();
}

uint32_t EpaSignalLabEpaVmHost::kernelThreadCount(size_t index) const {
    if (!module) {
        return 0u;
    }
    return epa_kernel_module_kernel_thread_count(module, index);
}

int EpaSignalLabEpaVmHost::findKernelIndex(const String &path_id) const {
    if (!module) {
        return -1;
    }
    String path_text(path_id);
    return epa_kernel_module_find_kernel(module, path_text.operator char *());
}

bool EpaSignalLabEpaVmHost::addKernelThreads(size_t index, uint32_t add_count) {
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

bool EpaSignalLabEpaVmHost::startKernel(size_t index) {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("startKernel failed", "no kernel bundle loaded");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_module_start_kernel(module, index, err)) {
        setError("epa_kernel_module_start_kernel failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaSignalLabEpaVmHost::stopKernel(size_t index) {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("stopKernel failed", "no kernel bundle loaded");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_module_stop_kernel(module, index, err)) {
        setError("epa_kernel_module_stop_kernel failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool EpaSignalLabEpaVmHost::startAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("startAllKernels failed", "no kernel bundle loaded");
        return false;
    }
    err[0] = 0;
    for (size_t i = 0; i < epa_kernel_module_count(module); i++) {
        EpaKernel *k = epa_kernel_module_kernel(module, i);
        if (!k) {
            continue;
        }
        uint32_t n = epa_kernel_worker_count(k);
        if (n > 0u) {
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

bool EpaSignalLabEpaVmHost::stopAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("stopAllKernels failed", "no kernel bundle loaded");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_module_stop_all_kernels(module, err)) {
        setError("epa_kernel_module_stop_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

const uint8_t *EpaSignalLabEpaVmHost::resultData(size_t *out_len) const {
    if (out_len) *out_len = 0;
    error_text = String("epa_kernel_get_result unavailable: the installed EPA runtime archive does not currently export epa_kernel_get_result");
    return NULL;
}

EpaKernel *EpaSignalLabEpaVmHost::rawKernel() const {
    return kernel;
}

EpaKernel *EpaSignalLabEpaVmHost::rawKernelAt(size_t index) const {
    if (module) {
        return epa_kernel_module_kernel(module, index);
    }
    return (index == 0u) ? kernel : NULL;
}

bool EpaSignalLabEpaVmHost::isReady() const {
    return kernel != NULL;
}

const String &EpaSignalLabEpaVmHost::lastError() const {
    return error_text;
}

}

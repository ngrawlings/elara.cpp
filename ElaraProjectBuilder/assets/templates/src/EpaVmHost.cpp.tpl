>>>>>>>>>>main>>>>CLASS_NAME
#include "%CLASS_NAME%.h"

#include <string.h>

namespace elara {

%CLASS_NAME%::%CLASS_NAME%()
    : kernel(NULL),
      module(NULL) {
}

%CLASS_NAME%::~%CLASS_NAME%() {
    destroy();
}

void %CLASS_NAME%::setError(const char *prefix, const char *detail) {
    error_text = String(prefix ? prefix : "EPA VM host error");
    if (detail && detail[0]) {
        error_text += String(": ");
        error_text += String(detail);
    }
}

bool %CLASS_NAME%::create() {
    char err[EPA_MAX_ERR];
    err[0] = 0;
    destroy();
    kernel = epa_kernel_create(err);
    if (!kernel) {
        setError("epa_kernel_create failed", err);
        return false;
    }
    error_text = String();
    return true;
}

void %CLASS_NAME%::destroy() {
    if (module) {
        epa_kernel_module_destroy(module);
        module = NULL;
    }
    if (kernel) {
        epa_kernel_destroy(kernel);
        kernel = NULL;
    }
}

bool %CLASS_NAME%::setKernelId(const String &kernel_id) {
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

bool %CLASS_NAME%::loadAsmPath(const String &asm_path) {
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
    error_text = String();
    return true;
}

bool %CLASS_NAME%::loadBlob(const uint8_t *blob, size_t blob_len) {
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
    error_text = String();
    return true;
}

bool %CLASS_NAME%::loadBundlePath(const String &bundle_path) {
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

bool %CLASS_NAME%::ingressPush(uint32_t wid, const void *data, uint32_t len) {
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

bool %CLASS_NAME%::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
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

bool %CLASS_NAME%::run(uint32_t max_ticks, bool debug) {
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

void %CLASS_NAME%::requestInterrupt() {
    if (kernel) {
        epa_kernel_request_interrupt(kernel);
    }
}

size_t %CLASS_NAME%::kernelCount() const {
    return module ? epa_kernel_module_count(module) : (kernel ? 1u : 0u);
}

String %CLASS_NAME%::kernelPathId(size_t index) const {
    const char *path_id = NULL;
    if (module) {
        path_id = epa_kernel_module_path_id(module, index);
    } else if (kernel && index == 0u) {
        path_id = "kernel";
    }
    return path_id ? String(path_id) : String();
}

EpaKernelStatus %CLASS_NAME%::kernelStatus(size_t index) const {
    if (module) {
        return epa_kernel_module_kernel_status(module, index);
    }
    if (kernel && index == 0u) {
        return epa_kernel_get_status(kernel);
    }
    return EPA_KERNEL_STATUS_ERROR;
}

String %CLASS_NAME%::kernelStatusText(size_t index) const {
    return String(epa_kernel_status_name(kernelStatus(index)));
}

String %CLASS_NAME%::kernelError(size_t index) const {
    const char *text = NULL;
    if (module) {
        text = epa_kernel_module_kernel_error(module, index);
    } else if (kernel && index == 0u) {
        text = epa_kernel_get_last_error(kernel);
    }
    return text ? String(text) : String();
}

bool %CLASS_NAME%::startKernel(size_t index) {
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

bool %CLASS_NAME%::stopKernel(size_t index) {
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

bool %CLASS_NAME%::startAllKernels() {
    char err[EPA_MAX_ERR];
    if (!module) {
        setError("startAllKernels failed", "no kernel bundle loaded");
        return false;
    }
    err[0] = 0;
    if (!epa_kernel_module_start_all_kernels(module, err)) {
        setError("epa_kernel_module_start_all_kernels failed", err);
        return false;
    }
    error_text = String();
    return true;
}

bool %CLASS_NAME%::stopAllKernels() {
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

const uint8_t *%CLASS_NAME%::resultData(size_t *out_len) const {
    if (out_len) *out_len = 0;
    error_text = String("epa_kernel_get_result unavailable: the installed EPA runtime archive does not currently export epa_kernel_get_result");
    return NULL;
}

EpaKernel *%CLASS_NAME%::rawKernel() const {
    return kernel;
}

EpaKernel *%CLASS_NAME%::rawKernelAt(size_t index) const {
    if (module) {
        return epa_kernel_module_kernel(module, index);
    }
    return (index == 0u) ? kernel : NULL;
}

bool %CLASS_NAME%::isReady() const {
    return kernel != NULL;
}

const String &%CLASS_NAME%::lastError() const {
    return error_text;
}

}
<<<<<<<<<<main

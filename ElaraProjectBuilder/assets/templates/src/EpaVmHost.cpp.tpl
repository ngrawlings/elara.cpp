>>>>>>>>>>main>>>>CLASS_NAME
#include "%CLASS_NAME%.h"

#include <string.h>

namespace elara {

%CLASS_NAME%::%CLASS_NAME%()
    : kernel(NULL) {
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
    if (!blob || !blob_len) {
        setError("epa_kernel_load_blob failed", "empty blob");
        return false;
    }
    (void)blob;
    (void)blob_len;
    setError("epa_kernel_load_blob unavailable", "the installed EPA runtime archive does not currently export epa_kernel_load_blob");
    return false;
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

const uint8_t *%CLASS_NAME%::resultData(size_t *out_len) const {
    if (out_len) *out_len = 0;
    error_text = String("epa_kernel_get_result unavailable: the installed EPA runtime archive does not currently export epa_kernel_get_result");
    return NULL;
}

EpaKernel *%CLASS_NAME%::rawKernel() const {
    return kernel;
}

bool %CLASS_NAME%::isReady() const {
    return kernel != NULL;
}

const String &%CLASS_NAME%::lastError() const {
    return error_text;
}

}
<<<<<<<<<<main

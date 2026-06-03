#include "EpaSystemLink.h"
#include <string.h>

namespace elara {

static const String empty_error;

class EpaCpuSystemBackend : public EpaSystemBackend {
public:
    EpaCpuSystemBackend() : kernel(NULL), module(NULL) {}
    virtual ~EpaCpuSystemBackend() { destroy(); }

    virtual bool createDebugKernel() {
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

    virtual void destroy() {
        if (module) { epa_kernel_module_destroy(module); module = NULL; }
        if (kernel) { epa_kernel_destroy(kernel); kernel = NULL; }
    }

    virtual bool setKernelId(const String &kernel_id) {
        char err[EPA_MAX_ERR];
        if (!kernel && !createDebugKernel()) return false;
        err[0] = 0;
        String text(kernel_id);
        if (!epa_kernel_set_id(kernel, text.operator char *(), err)) {
            setError("epa_kernel_set_id failed", err);
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool loadAsmPath(const String &asm_path) {
        char err[EPA_MAX_ERR];
        if (!kernel && !createDebugKernel()) return false;
        err[0] = 0;
        String text(asm_path);
        if (!epa_kernel_load_asm(kernel, text.operator char *(), err)) {
            setError("epa_kernel_load_asm failed", err);
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool loadBlob(const uint8_t *blob, size_t blob_len) {
        char err[EPA_MAX_ERR];
        if (!kernel && !createDebugKernel()) return false;
        err[0] = 0;
        if (!blob || !blob_len) { setError("epa_kernel_load_blob failed", "empty blob"); return false; }
        if (!epa_kernel_load_blob(kernel, blob, blob_len, err)) {
            setError("epa_kernel_load_blob failed", err);
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool loadBundlePath(const String &bundle_path) {
        char err[EPA_MAX_ERR];
        String text(bundle_path);
        err[0] = 0;
        destroy();
        module = epa_kernel_module_load_bundle(text.operator char *(), err);
        if (!module) { setError("epa_kernel_module_load_bundle failed", err); return false; }
        size_t count = epa_kernel_module_count(module);
        for (size_t i = 0; i < count; i++) {
            EpaKernel *k = epa_kernel_module_kernel(module, i);
            if (!k) continue;
            epa_kernel_set_scheduler(k, EPA_SCHED_DEBUG, err);
        }
        error_text = String();
        return true;
    }

    virtual bool ingressPush(uint32_t wid, const void *data, uint32_t len) {
        if (!kernel) { setError("ingressPush failed", "kernel not created"); return false; }
        if (!epa_kernel_ingress_push(kernel, wid, data, len)) {
            setError("epa_kernel_ingress_push failed", "queue full or invalid wid");
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
        if (!kernel) { setError("ingressPushTagged failed", "kernel not created"); return false; }
        if (!epa_kernel_ingress_push_tagged(kernel, wid, tag, data, len)) {
            setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid wid");
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
        EpaKernel *target = rawKernelAt(index);
        if (!target) { setError("ingressPushToKernel failed", "kernel index out of range"); return false; }
        if (!epa_kernel_ingress_push(target, wid, data, len)) {
            setError("epa_kernel_ingress_push failed", "queue full or invalid wid");
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
        EpaKernel *target = rawKernelAt(index);
        if (!target) { setError("ingressPushTaggedToKernel failed", "kernel index out of range"); return false; }
        if (!epa_kernel_ingress_push_tagged(target, wid, tag, data, len)) {
            setError("epa_kernel_ingress_push_tagged failed", "queue full or invalid wid");
            return false;
        }
        error_text = String();
        return true;
    }

    virtual EpaKernel *rawKernel() const { return kernel; }

    virtual EpaKernel *rawKernelAt(size_t index) const {
        return module ? epa_kernel_module_kernel(module, index) : NULL;
    }

    virtual size_t kernelCount() const {
        return module ? epa_kernel_module_count(module) : 0;
    }

    virtual String kernelPathId(size_t index) const {
        const char *id = module ? epa_kernel_module_path_id(module, index) : NULL;
        return id ? String(id) : String();
    }

    virtual String kernelStatus(size_t index) const {
        if (!module) return String();
        return String(epa_kernel_status_name(epa_kernel_module_kernel_status(module, index)));
    }

    virtual String kernelError(size_t index) const {
        const char *e = module ? epa_kernel_module_kernel_error(module, index) : NULL;
        return e ? String(e) : String();
    }

    virtual uint32_t kernelThreadCount(size_t index) const {
        return module ? epa_kernel_module_kernel_thread_count(module, index) : 0;
    }

    virtual int findKernelIndex(const String &path_id) const {
        if (!module) return -1;
        String text(path_id);
        return epa_kernel_module_find_kernel(module, text.operator char *());
    }

    virtual bool addKernelThreads(size_t index, uint32_t add_count) {
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

    virtual bool startAllKernels() {
        char err[EPA_MAX_ERR];
        if (!module) { setError("startAllKernels failed", "module not loaded"); return false; }
        err[0] = 0;
        size_t count = epa_kernel_module_count(module);
        for (size_t i = 0; i < count; i++) {
            EpaKernel *k = epa_kernel_module_kernel(module, i);
            if (!k) continue;
            epa_kernel_set_scheduler(k, EPA_SCHED_DEBUG, err);
        }
        if (!epa_kernel_module_start_all_kernels(module, err)) {
            setError("epa_kernel_module_start_all_kernels failed", err);
            return false;
        }
        error_text = String();
        return true;
    }

    virtual bool stopAllKernels() {
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

    virtual bool isReady() const { return kernel != NULL || module != NULL; }
    virtual const String &lastError() const { return error_text; }

private:
    EpaKernel       *kernel;
    EpaKernelModule *module;
    String           error_text;

    void setError(const char *prefix, const char *detail) {
        error_text = String(prefix ? prefix : "EPAVM CPU backend error");
        if (detail && detail[0]) {
            error_text += String(": ");
            error_text += String(detail);
        }
    }
};

EpaSystemLink::EpaSystemLink() : backend(NULL), error_text() {
    useCpuBackend();
}

EpaSystemLink::~EpaSystemLink() {
    destroy();
    delete backend;
    backend = NULL;
}

bool EpaSystemLink::useCpuBackend() {
    if (backend) {
        backend->destroy();
        delete backend;
    }
    backend = new EpaCpuSystemBackend();
    error_text = String();
    return backend != NULL;
}

bool EpaSystemLink::ensureBackend() {
    if (backend) return true;
    return useCpuBackend();
}

bool EpaSystemLink::createDebugKernel() {
    return ensureBackend() && backend->createDebugKernel();
}

void EpaSystemLink::destroy() {
    if (backend) backend->destroy();
}

bool EpaSystemLink::setKernelId(const String &kernel_id) {
    return ensureBackend() && backend->setKernelId(kernel_id);
}

bool EpaSystemLink::loadAsmPath(const String &asm_path) {
    return ensureBackend() && backend->loadAsmPath(asm_path);
}

bool EpaSystemLink::loadBlob(const uint8_t *blob, size_t blob_len) {
    return ensureBackend() && backend->loadBlob(blob, blob_len);
}

bool EpaSystemLink::loadBundlePath(const String &bundle_path) {
    return ensureBackend() && backend->loadBundlePath(bundle_path);
}

bool EpaSystemLink::ingressPush(uint32_t wid, const void *data, uint32_t len) {
    return ensureBackend() && backend->ingressPush(wid, data, len);
}

bool EpaSystemLink::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    return ensureBackend() && backend->ingressPushTagged(wid, tag, data, len);
}

bool EpaSystemLink::ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
    return ensureBackend() && backend->ingressPushToKernel(index, wid, data, len);
}

bool EpaSystemLink::ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    return ensureBackend() && backend->ingressPushTaggedToKernel(index, wid, tag, data, len);
}

EpaKernel *EpaSystemLink::rawKernel() const {
    return backend ? backend->rawKernel() : NULL;
}

EpaKernel *EpaSystemLink::rawKernelAt(size_t index) const {
    return backend ? backend->rawKernelAt(index) : NULL;
}

size_t EpaSystemLink::kernelCount() const {
    return backend ? backend->kernelCount() : 0;
}

String EpaSystemLink::kernelPathId(size_t index) const {
    return backend ? backend->kernelPathId(index) : String();
}

String EpaSystemLink::kernelStatus(size_t index) const {
    return backend ? backend->kernelStatus(index) : String();
}

String EpaSystemLink::kernelError(size_t index) const {
    return backend ? backend->kernelError(index) : String();
}

uint32_t EpaSystemLink::kernelThreadCount(size_t index) const {
    return backend ? backend->kernelThreadCount(index) : 0;
}

int EpaSystemLink::findKernelIndex(const String &path_id) const {
    return backend ? backend->findKernelIndex(path_id) : -1;
}

bool EpaSystemLink::addKernelThreads(size_t index, uint32_t add_count) {
    return ensureBackend() && backend->addKernelThreads(index, add_count);
}

bool EpaSystemLink::startAllKernels() {
    return ensureBackend() && backend->startAllKernels();
}

bool EpaSystemLink::stopAllKernels() {
    return ensureBackend() && backend->stopAllKernels();
}

bool EpaSystemLink::isReady() const {
    return backend && backend->isReady();
}

const String &EpaSystemLink::lastError() const {
    return backend ? backend->lastError() : error_text;
}

} // namespace elara

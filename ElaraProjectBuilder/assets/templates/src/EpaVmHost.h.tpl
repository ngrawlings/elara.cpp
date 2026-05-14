>>>>>>>>>>main>>>>CLASS_NAME>CLASS_NAME_UPPER
#ifndef %CLASS_NAME_UPPER%_H
#define %CLASS_NAME_UPPER%_H

#include <stddef.h>
#include <stdint.h>
#include <libelaracore/memory/String.h>

extern "C" {
typedef struct EpaKernel EpaKernel;
typedef struct EpaKernelModule EpaKernelModule;
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif
typedef enum {
    EPA_KERNEL_STATUS_UNLOADED = 0,
    EPA_KERNEL_STATUS_LOADED   = 1,
    EPA_KERNEL_STATUS_RUNNING  = 2,
    EPA_KERNEL_STATUS_STOPPED  = 3,
    EPA_KERNEL_STATUS_HALTED   = 4,
    EPA_KERNEL_STATUS_FAULTED  = 5,
    EPA_KERNEL_STATUS_ERROR    = 6
} EpaKernelStatus;
EpaKernel* epa_kernel_create(char err[EPA_MAX_ERR]);
void epa_kernel_destroy(EpaKernel *k);
int epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]);
int epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
int epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);
int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len);
int epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);
EpaKernelStatus epa_kernel_get_status(const EpaKernel *k);
const char* epa_kernel_get_last_error(const EpaKernel *k);
const char* epa_kernel_status_name(EpaKernelStatus status);
EpaKernelModule* epa_kernel_module_load_bundle(const char *bundle_path, char err[EPA_MAX_ERR]);
void epa_kernel_module_destroy(EpaKernelModule *module);
size_t epa_kernel_module_count(const EpaKernelModule *module);
const char* epa_kernel_module_path_id(const EpaKernelModule *module, size_t index);
uint32_t epa_kernel_module_flags(const EpaKernelModule *module, size_t index);
EpaKernel* epa_kernel_module_kernel(const EpaKernelModule *module, size_t index);
int epa_kernel_module_find_kernel(const EpaKernelModule *module, const char *path_id);
EpaKernelStatus epa_kernel_module_kernel_status(const EpaKernelModule *module, size_t index);
const char* epa_kernel_module_kernel_error(const EpaKernelModule *module, size_t index);
int epa_kernel_module_start_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]);
int epa_kernel_module_stop_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]);
int epa_kernel_module_start_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);
int epa_kernel_module_stop_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);
}

namespace elara {

class %CLASS_NAME% {
public:
    %CLASS_NAME%();
    ~%CLASS_NAME%();

    bool create();
    void destroy();
    bool setKernelId(const String &kernel_id);
    bool loadAsmPath(const String &asm_path);
    bool loadBlob(const uint8_t *blob, size_t blob_len);
    bool loadBundlePath(const String &bundle_path);
    bool ingressPush(uint32_t wid, const void *data, uint32_t len);
    bool ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len);
    bool run(uint32_t max_ticks, bool debug);
    void requestInterrupt();
    size_t kernelCount() const;
    String kernelPathId(size_t index) const;
    EpaKernelStatus kernelStatus(size_t index) const;
    String kernelStatusText(size_t index) const;
    String kernelError(size_t index) const;
    bool startKernel(size_t index);
    bool stopKernel(size_t index);
    bool startAllKernels();
    bool stopAllKernels();
    const uint8_t *resultData(size_t *out_len) const;
    EpaKernel *rawKernel() const;
    EpaKernel *rawKernelAt(size_t index) const;
    bool isReady() const;
    const String &lastError() const;

private:
    EpaKernel *kernel;
    EpaKernelModule *module;
    mutable String error_text;

    void setError(const char *prefix, const char *detail);
};

}

#endif
<<<<<<<<<<main

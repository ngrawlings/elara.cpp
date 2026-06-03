#ifndef EPASYSTEMLINK_H
#define EPASYSTEMLINK_H

#include <stddef.h>
#include <stdint.h>
#include <libelaracore/memory/String.h>

extern "C" {
typedef struct EpaKernel EpaKernel;
typedef struct EpaKernelModule EpaKernelModule;
typedef enum {
    EPA_KERNEL_STATUS_UNLOADED = 0,
    EPA_KERNEL_STATUS_LOADED   = 1,
    EPA_KERNEL_STATUS_RUNNING  = 2,
    EPA_KERNEL_STATUS_STOPPED  = 3,
    EPA_KERNEL_STATUS_HALTED   = 4,
    EPA_KERNEL_STATUS_FAULTED  = 5,
    EPA_KERNEL_STATUS_ERROR    = 6
} EpaKernelStatus;
typedef enum {
    EPA_SCHED_WAVE       = 1,
    EPA_SCHED_CPU_THREAD = 2,
    EPA_SCHED_CUDA_HOTIDLE = 3,
    EPA_SCHED_DEBUG      = 4,
} EpaSchedProfile;
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif
EpaKernel*       epa_kernel_create(char err[EPA_MAX_ERR]);
int              epa_kernel_set_scheduler(EpaKernel *k, EpaSchedProfile profile, char err[EPA_MAX_ERR]);
void             epa_kernel_destroy(EpaKernel *k);
int              epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]);
int              epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
int              epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);
int              epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
int              epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len);
int              epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void             epa_kernel_request_interrupt(EpaKernel *k);
int              epa_kernel_add_threads(EpaKernel *k, uint32_t add_count, char err[EPA_MAX_ERR]);
uint32_t         epa_kernel_worker_count(const EpaKernel *k);
EpaKernelStatus  epa_kernel_get_status(const EpaKernel *k);
const char*      epa_kernel_status_name(EpaKernelStatus status);
void             epa_kernel_set_debug_callback(EpaKernel *k, void *cb, void *cb_user);
EpaKernelModule* epa_kernel_module_load_bundle(const char *bundle_path, char err[EPA_MAX_ERR]);
void             epa_kernel_module_destroy(EpaKernelModule *module);
size_t           epa_kernel_module_count(const EpaKernelModule *module);
const char*      epa_kernel_module_path_id(const EpaKernelModule *module, size_t index);
EpaKernelStatus  epa_kernel_module_kernel_status(const EpaKernelModule *module, size_t index);
const char*      epa_kernel_module_kernel_error(const EpaKernelModule *module, size_t index);
int              epa_kernel_module_start_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);
int              epa_kernel_module_stop_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);
int              epa_kernel_module_find_kernel(const EpaKernelModule *module, const char *path_id);
EpaKernel*       epa_kernel_module_kernel(const EpaKernelModule *module, size_t index);
int              epa_kernel_module_add_kernel_threads(EpaKernelModule *module, size_t index, uint32_t add_count, char err[EPA_MAX_ERR]);
uint32_t         epa_kernel_module_kernel_thread_count(const EpaKernelModule *module, size_t index);
}

namespace elara {

enum EpaSysFrameKind {
    EPA_SYS_INGRESS      = 1,
    EPA_SYS_EGRESS       = 2,
    EPA_SYS_HOST_SIGNAL  = 3,
    EPA_SYS_AT_REQUEST   = 4,
    EPA_SYS_AT_RESPONSE  = 5,
    EPA_SYS_MEM_REGISTER = 6,
    EPA_SYS_MEM_RELEASE  = 7,
    EPA_SYS_DEBUG_EVENT  = 8,
    EPA_SYS_FAULT        = 9,
    EPA_SYS_CONTROL      = 10
};

enum EpaSysMemorySpace {
    EPA_SYS_MEM_CPU         = 1,
    EPA_SYS_MEM_CUDA_DEVICE = 2,
    EPA_SYS_MEM_CUDA_PINNED = 3,
    EPA_SYS_MEM_SHARED      = 4
};

enum EpaSysMemoryAccess {
    EPA_SYS_MEM_READ       = 1,
    EPA_SYS_MEM_WRITE      = 2,
    EPA_SYS_MEM_READ_WRITE = 3
};

struct EpaSysFrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint16_t kind;
    uint16_t flags;
    uint32_t frame_bytes;
    uint64_t seq;
    uint64_t correlation_id;
    uint32_t kernel_id;
    uint32_t worker_id;
    uint32_t payload_bytes;
    uint32_t ref_count;
};

struct EpaSysMemoryRef {
    uint64_t handle;
    uint64_t offset;
    uint64_t bytes;
    uint32_t space;
    uint32_t access;
    uint32_t element_type;
    uint32_t element_bytes;
};

struct EpaSysAtRequest {
    uint32_t at_id;
    uint32_t at_version;
    uint32_t requested_threads;
    uint32_t flags;
    uint32_t param_bytes;
    uint32_t result_ref_index;
};

struct EpaSysAtResponse {
    uint32_t status;
    uint32_t result_code;
    uint32_t threads_used;
    uint32_t payload_bytes;
};

class EpaSystemBackend {
public:
    virtual ~EpaSystemBackend() {}

    virtual bool createDebugKernel() = 0;
    virtual void destroy() = 0;
    virtual bool setKernelId(const String &kernel_id) = 0;
    virtual bool loadAsmPath(const String &asm_path) = 0;
    virtual bool loadBlob(const uint8_t *blob, size_t blob_len) = 0;
    virtual bool loadBundlePath(const String &bundle_path) = 0;
    virtual bool ingressPush(uint32_t wid, const void *data, uint32_t len) = 0;
    virtual bool ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) = 0;
    virtual bool ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) = 0;
    virtual bool ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) = 0;

    virtual EpaKernel       *rawKernel() const = 0;
    virtual EpaKernel       *rawKernelAt(size_t index) const = 0;
    virtual size_t           kernelCount() const = 0;
    virtual String           kernelPathId(size_t index) const = 0;
    virtual String           kernelStatus(size_t index) const = 0;
    virtual String           kernelError(size_t index) const = 0;
    virtual uint32_t         kernelThreadCount(size_t index) const = 0;
    virtual int              findKernelIndex(const String &path_id) const = 0;
    virtual bool             addKernelThreads(size_t index, uint32_t add_count) = 0;
    virtual bool             startAllKernels() = 0;
    virtual bool             stopAllKernels() = 0;
    virtual bool             isReady() const = 0;
    virtual const String    &lastError() const = 0;
};

class EpaSystemLink {
public:
    EpaSystemLink();
    ~EpaSystemLink();

    bool useCpuBackend();
    bool createDebugKernel();
    void destroy();
    bool setKernelId(const String &kernel_id);
    bool loadAsmPath(const String &asm_path);
    bool loadBlob(const uint8_t *blob, size_t blob_len);
    bool loadBundlePath(const String &bundle_path);
    bool ingressPush(uint32_t wid, const void *data, uint32_t len);
    bool ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len);
    bool ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len);
    bool ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len);

    EpaKernel       *rawKernel() const;
    EpaKernel       *rawKernelAt(size_t index) const;
    size_t           kernelCount() const;
    String           kernelPathId(size_t index) const;
    String           kernelStatus(size_t index) const;
    String           kernelError(size_t index) const;
    uint32_t         kernelThreadCount(size_t index) const;
    int              findKernelIndex(const String &path_id) const;
    bool             addKernelThreads(size_t index, uint32_t add_count);
    bool             startAllKernels();
    bool             stopAllKernels();
    bool             isReady() const;
    const String    &lastError() const;

private:
    EpaSystemBackend *backend;
    String           error_text;

    bool ensureBackend();
};

} // namespace elara

#endif

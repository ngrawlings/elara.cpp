#ifndef EPADBGVMHOST_H
#define EPADBGVMHOST_H

#include <stddef.h>
#include <stdint.h>
#include "EpaSystemLink.h"

namespace elara {

class EpaDbgVmHost {
public:
    EpaDbgVmHost();
    ~EpaDbgVmHost();

    bool create();
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
    EpaSystemLink link;
};

} // namespace elara

#endif

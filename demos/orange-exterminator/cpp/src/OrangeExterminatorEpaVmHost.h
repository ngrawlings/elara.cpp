#ifndef ORANGEEXTERMINATOREPAVMHOST_H
#define ORANGEEXTERMINATOREPAVMHOST_H

#include <stddef.h>
#include <stdint.h>
#include <libelaracore/memory/String.h>

extern "C" {
typedef struct EpaKernel EpaKernel;
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif
EpaKernel* epa_kernel_create(char err[EPA_MAX_ERR]);
void epa_kernel_destroy(EpaKernel *k);
int epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]);
int epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len);
int epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);
}

namespace elara {

class OrangeExterminatorEpaVmHost {
public:
    OrangeExterminatorEpaVmHost();
    ~OrangeExterminatorEpaVmHost();

    bool create();
    void destroy();
    bool setKernelId(const String &kernel_id);
    bool loadAsmPath(const String &asm_path);
    bool loadBlob(const uint8_t *blob, size_t blob_len);
    bool ingressPush(uint32_t wid, const void *data, uint32_t len);
    bool ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len);
    bool run(uint32_t max_ticks, bool debug);
    void requestInterrupt();
    const uint8_t *resultData(size_t *out_len) const;
    EpaKernel *rawKernel() const;
    bool isReady() const;
    const String &lastError() const;

private:
    EpaKernel *kernel;
    mutable String error_text;

    void setError(const char *prefix, const char *detail);
};

}

#endif

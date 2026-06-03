#include "EpaDbgVmHost.h"

namespace elara {

EpaDbgVmHost::EpaDbgVmHost() {}

EpaDbgVmHost::~EpaDbgVmHost() { destroy(); }

bool EpaDbgVmHost::create() {
    return link.createDebugKernel();
}

void EpaDbgVmHost::destroy() {
    link.destroy();
}

bool EpaDbgVmHost::setKernelId(const String &kernel_id) {
    return link.setKernelId(kernel_id);
}

bool EpaDbgVmHost::loadAsmPath(const String &asm_path) {
    return link.loadAsmPath(asm_path);
}

bool EpaDbgVmHost::loadBlob(const uint8_t *blob, size_t blob_len) {
    return link.loadBlob(blob, blob_len);
}

bool EpaDbgVmHost::loadBundlePath(const String &bundle_path) {
    return link.loadBundlePath(bundle_path);
}

bool EpaDbgVmHost::ingressPush(uint32_t wid, const void *data, uint32_t len) {
    return link.ingressPush(wid, data, len);
}

bool EpaDbgVmHost::ingressPushTagged(uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    return link.ingressPushTagged(wid, tag, data, len);
}

bool EpaDbgVmHost::ingressPushToKernel(size_t index, uint32_t wid, const void *data, uint32_t len) {
    return link.ingressPushToKernel(index, wid, data, len);
}

bool EpaDbgVmHost::ingressPushTaggedToKernel(size_t index, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
    return link.ingressPushTaggedToKernel(index, wid, tag, data, len);
}

EpaKernel *EpaDbgVmHost::rawKernel() const {
    return link.rawKernel();
}

EpaKernel *EpaDbgVmHost::rawKernelAt(size_t index) const {
    return link.rawKernelAt(index);
}

size_t EpaDbgVmHost::kernelCount() const {
    return link.kernelCount();
}

String EpaDbgVmHost::kernelPathId(size_t index) const {
    return link.kernelPathId(index);
}

String EpaDbgVmHost::kernelStatus(size_t index) const {
    return link.kernelStatus(index);
}

String EpaDbgVmHost::kernelError(size_t index) const {
    return link.kernelError(index);
}

uint32_t EpaDbgVmHost::kernelThreadCount(size_t index) const {
    return link.kernelThreadCount(index);
}

int EpaDbgVmHost::findKernelIndex(const String &path_id) const {
    return link.findKernelIndex(path_id);
}

bool EpaDbgVmHost::addKernelThreads(size_t index, uint32_t add_count) {
    return link.addKernelThreads(index, add_count);
}

bool EpaDbgVmHost::startAllKernels() {
    return link.startAllKernels();
}

bool EpaDbgVmHost::stopAllKernels() {
    return link.stopAllKernels();
}

bool EpaDbgVmHost::isReady() const {
    return link.isReady();
}

const String &EpaDbgVmHost::lastError() const {
    return link.lastError();
}

} // namespace elara

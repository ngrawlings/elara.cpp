#include "ElaraWidgetHandle.h"

#include <libelaracore/memory/ByteArray.h>

namespace elara {

ElaraWidgetHandle::ElaraWidgetHandle() {

}

ElaraWidgetHandle::ElaraWidgetHandle(const char* hex) {
    handle = Memory(ByteArray::fromHex(hex));
}

ElaraWidgetHandle::ElaraWidgetHandle(const ElaraWidgetHandle& inst) : handle(inst.handle) {

}

ElaraWidgetHandle::~ElaraWidgetHandle() {

}

Memory ElaraWidgetHandle::getHandle() const {
    return handle;
}

}
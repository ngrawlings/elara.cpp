#include "ElaraWidgetHandle.h"

#include <libelaracore/memory/ByteArray.h>

namespace elara {

ElaraWidgetHandle::ElaraWidgetHandle() {

}

ElaraWidgetHandle::ElaraWidgetHandle(const String& id) {
    handle = Memory((const char*)id, id.byteLength());
}

ElaraWidgetHandle::ElaraWidgetHandle(const ElaraWidgetHandle& inst) : handle(inst.handle) {

}

ElaraWidgetHandle::~ElaraWidgetHandle() {

}

Memory ElaraWidgetHandle::getHandle() const {
    return handle;
}

}
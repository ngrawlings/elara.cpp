#ifndef ELARA_WIDGET_HANDLE_H
#define ELARA_WIDGET_HANDLE_H

#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

namespace elara {

class ElaraWidgetHandle {
public:
    ElaraWidgetHandle();
    ElaraWidgetHandle(const char* bytes);
    ElaraWidgetHandle(const ElaraWidgetHandle& inst);
    virtual ~ElaraWidgetHandle();

    Memory getHandle() const;

protected:
    Memory handle;
};

}

#endif
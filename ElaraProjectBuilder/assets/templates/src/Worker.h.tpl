#ifndef %WorkerName%_h
#define %WorkerName%_h

#include <libelarathreads/Task.h>
#include <libelaracore/memory/String.h>

class %WorkerName% : public elara::Task {
public:
    %WorkerName%(elara::String payload);
    virtual ~%WorkerName%();

protected:
    virtual void run();

private:
    elara::String payload;
};

#endif

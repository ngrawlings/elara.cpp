>>>>>>>>>>main>>>>WORKER_NAME
#ifndef %WORKER_NAME%_h
#define %WORKER_NAME%_h

#include <libelarathreads/Task.h>
#include <libelaracore/memory/String.h>

class %WORKER_NAME% : public elara::Task {
public:
    %WORKER_NAME%(elara::String payload);
    virtual ~%WORKER_NAME%();

protected:
    virtual void run();

private:
    elara::String payload;
};

#endif
<<<<<<<<<<main

//
//  DescriptorInstanceMap.h
//  libElaraSockets
//
//  Descriptor-indexed socket instance table.
//

#ifndef __libElaraSockets__memory__DescriptorInstanceMap__
#define __libElaraSockets__memory__DescriptorInstanceMap__

#include <libelaracore/types.h>

namespace elara {
namespace sockets {
namespace memory {

    template <class T>
    class DescriptorInstanceMap {
    public:
        DescriptorInstanceMap() {
            max = (unsigned int)sysconf(_SC_OPEN_MAX);
            descriptors = new T[max];
            for (unsigned int i = 0; i < max; i++)
                descriptors[i] = 0;
        }

        virtual ~DescriptorInstanceMap() {
            delete [] descriptors;
        }

        void set(unsigned long fd, T instance) {
            descriptors[fd] = instance;
        }

        bool equals(unsigned long fd, T instance) {
            return descriptors[fd] == instance;
        }

        T get(unsigned long fd) {
            return descriptors[fd];
        }

        unsigned int getMaxDescriptors() {
            return max;
        }

    private:
        unsigned int max;
        T *descriptors;
    };

}
}
}

#endif /* defined(__libElaraSockets__memory__DescriptorInstanceMap__) */

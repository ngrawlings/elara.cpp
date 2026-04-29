//
//  Ref.h
//  libElaraThreads
//
//  Thread-safe shared ownership wrapper for cross-thread lifetime management.
//

#ifndef __libElaraThreads__memory__Ref__
#define __libElaraThreads__memory__Ref__

#include <atomic>

namespace elara {
namespace threading {
namespace memory {

    template <class T>
    class Ref {
    public:
        explicit Ref(T *ptr) : ptr(ptr), cnt(0), array(false) {
            if (ptr)
                cnt = new std::atomic<int>(1);
        }

        explicit Ref(T *ptr, bool array) : ptr(ptr), cnt(0), array(array) {
            if (ptr)
                cnt = new std::atomic<int>(1);
        }

        Ref(const Ref<T> &ref) : ptr(ref.ptr), cnt(ref.cnt), array(ref.array) {
            if (cnt)
                cnt->fetch_add(1, std::memory_order_relaxed);
        }

        Ref(Ref<T> &&ref) : ptr(ref.ptr), cnt(ref.cnt), array(ref.array) {
            ref.ptr = 0;
            ref.cnt = 0;
            ref.array = false;
        }

        Ref() : ptr(0), cnt(0), array(false) {
        }

        virtual ~Ref() {
            decrement();
        }

        T &operator*() {
            return *ptr;
        }

        const T &operator*() const {
            return *ptr;
        }

        T *operator->() {
            return ptr;
        }

        const T *operator->() const {
            return ptr;
        }

        explicit operator bool() const {
            return ptr != 0;
        }

        T &get() {
            return *ptr;
        }

        const T &get() const {
            return *ptr;
        }

        T *getPtr() {
            return ptr;
        }

        const T *getPtr() const {
            return ptr;
        }

        Ref<T> &operator=(const Ref<T> &ref) {
            if (this == &ref)
                return *this;

            decrement();

            ptr = ref.ptr;
            cnt = ref.cnt;
            array = ref.array;

            if (cnt)
                cnt->fetch_add(1, std::memory_order_relaxed);

            return *this;
        }

        Ref<T> &operator=(Ref<T> &&ref) {
            if (this == &ref)
                return *this;

            decrement();

            ptr = ref.ptr;
            cnt = ref.cnt;
            array = ref.array;

            ref.ptr = 0;
            ref.cnt = 0;
            ref.array = false;

            return *this;
        }

        void release() {
            decrement();
            ptr = 0;
            cnt = 0;
            array = false;
        }

    protected:
        T *ptr;
        std::atomic<int> *cnt;
        bool array;

    private:
        void decrement() {
            if (!cnt)
                return;

            if (cnt->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                if (ptr != 0) {
                    if (array)
                        delete [] ptr;
                    else
                        delete ptr;
                }
                delete cnt;
            }

            ptr = 0;
            cnt = 0;
            array = false;
        }
    };

}
}
}

namespace elara {
namespace threading {

template <class T>
using Ref = ::elara::threading::memory::Ref<T>;

}
}

#endif /* defined(__libElaraThreads__memory__Ref__) */

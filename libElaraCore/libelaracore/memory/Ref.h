//
//  Ref.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 02/11/2012.
//  Copyright (c) 2013. All rights reserved.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// For affordable commercial licensing please contact ngrawlings@gmail.com
//

#ifndef __Elara__Ref__
#define __Elara__Ref__

#define REF(X) X.get()

namespace elara {

    template <class T>
    class Ref {
    public:
        static Ref<T> borrow(T* ptr) {
            Ref<T> ref;
            ref.ptr = ptr;
            ref.cnt = 0;
            ref.array = false;
            ref.borrowed = true;
            return ref;
        }

        explicit Ref<T>(T* ptr) {
            this->ptr = ptr;
            if (ptr) {
                cnt = new int;
                (*cnt) = 1;
            } else
                cnt = 0;
            this->array = false;
            this->borrowed = false;
        }
        
        explicit Ref<T>(T* ptr, bool array){
            this->ptr = ptr;
            if (ptr) {
                cnt = new int;
                (*cnt) = 1;
            } else
                cnt = 0;
            this->array = array;
            this->borrowed = false;
        }
        
        Ref<T>(const Ref<T>& ref) {
            ptr = ref.ptr;
            borrowed = ref.borrowed;
            if (ref.ptr && !borrowed) {
                cnt = ref.cnt;
                *cnt = (*cnt)+1;
            } else
                cnt = 0;
            array = ref.array;
        }

        Ref<T>(Ref<T>&& ref) {
            ptr = ref.ptr;
            cnt = ref.cnt;
            array = ref.array;
            borrowed = ref.borrowed;

            ref.ptr = 0;
            ref.cnt = 0;
            ref.array = false;
            ref.borrowed = false;
        }
        
        Ref<T>() {
            ptr = 0;
            cnt = 0;
            array = false;
            borrowed = false;
        }
        
        virtual ~Ref () {
            decrement();
        }

        T& operator*() {
            return *ptr;
        }

        const T& operator*() const {
            return *ptr;
        }

        T* operator->() {
            return ptr;
        }

        const T* operator->() const {
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
        
        Ref<T>& operator= (const Ref<T>& ref) {
            if (this == &ref)
                return *this;

            decrement();
            
            ptr = ref.ptr;
            cnt = ref.cnt;
            array = ref.array;
            borrowed = ref.borrowed;
            
            if (cnt && !borrowed)
                *cnt = (*cnt)+1;
            
            return *this;
        }

        Ref<T>& operator= (Ref<T>&& ref) {
            if (this == &ref)
                return *this;

            decrement();

            ptr = ref.ptr;
            cnt = ref.cnt;
            array = ref.array;
            borrowed = ref.borrowed;

            ref.ptr = 0;
            ref.cnt = 0;
            ref.array = false;
            ref.borrowed = false;

            return *this;
        }
        
        void release() {
            ptr = 0;
            cnt = 0;
        }
        
    protected:
        T *ptr;
        int *cnt;
        bool array;
        bool borrowed;
        
    private:
        void decrement() {
            if (borrowed) {
                ptr = 0;
                cnt = 0;
                borrowed = false;
                return;
            }
            if (cnt) {
                (*cnt)--;
                if (*cnt == 0 && ptr != 0) {
                    if (array)
                        delete [] ptr;
                    else
                        delete ptr;
                    delete cnt;
                    ptr = 0;
                    cnt = 0;
                } else if(*cnt <= 0) {
                    delete cnt;
                    cnt = 0;
                }
            }
        }
    };
    
};

namespace elara {
namespace core {
namespace memory {
    template <class T>
    using Ref = ::elara::Ref<T>;
}
}
}

#endif /* defined(__Elara__Ref__) */

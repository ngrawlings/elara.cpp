//
//  InstancePool.h
//  libElaraThreads
//
//  Thread-safe reusable instance pool for scarce server-side resources.
//

#ifndef __libElaraThreads__memory__InstancePool__
#define __libElaraThreads__memory__InstancePool__

#include <libelaracore/memory/LinkedList.h>

#include <pthread.h>
#include <time.h>
#include <type_traits>

namespace elara {
namespace threading {
namespace memory {

    template <class T>
    class InstancePool {
    public:
        typedef void *HANDLE;

        typedef enum {
            WAIT,
            BUSY
        } STATUS;

    private:
        typedef struct ENTRY {
            T inst;
            STATUS status;
            bool retired;
        } ENTRY;

    public:
        explicit InstancePool(bool auto_free=false)
            : auto_free(auto_free), shutting_down(false) {
            pthread_mutex_init(&mutex, 0);
            pthread_cond_init(&available_condition, 0);
        }

        virtual ~InstancePool() {
            pthread_mutex_lock(&mutex);
            shutting_down = true;
            pthread_cond_broadcast(&available_condition);

            while (entries.length()) {
                ENTRY *entry = entries.get(entries.firstNode());
                entries.removeNode(entries.firstNode());
                destroyEntry(entry);
            }

            available.clear();
            pthread_mutex_unlock(&mutex);

            pthread_cond_destroy(&available_condition);
            pthread_mutex_destroy(&mutex);
        }

        void add(const T &obj) {
            ENTRY *entry = new ENTRY;
            entry->inst = obj;
            entry->status = WAIT;
            entry->retired = false;

            pthread_mutex_lock(&mutex);
            entries.add(entry);
            available.add(entry);
            pthread_cond_signal(&available_condition);
            pthread_mutex_unlock(&mutex);
        }

        void remove(const T &obj) {
            pthread_mutex_lock(&mutex);

            LINKEDLIST_NODE_HANDLE node = entries.firstNode();
            if (node) {
                do {
                    LINKEDLIST_NODE_HANDLE current = node;
                    node = entries.nextNode(current);

                    ENTRY *entry = entries.get(current);
                    if (entry->inst == obj) {
                        if (entry->status == WAIT) {
                            removeAvailableEntry(entry);
                            entries.removeNode(current);
                            destroyEntry(entry);
                        } else {
                            entry->retired = true;
                        }
                        break;
                    }
                } while (node && node != entries.firstNode());
            }

            pthread_mutex_unlock(&mutex);
        }

        int length() {
            pthread_mutex_lock(&mutex);
            int count = entries.length();
            pthread_mutex_unlock(&mutex);
            return count;
        }

        int getSize() {
            return length();
        }

        HANDLE acquire(bool block, long timeout_ms=0) {
            pthread_mutex_lock(&mutex);

            while (true) {
                ENTRY *entry = popAvailableEntry();
                if (entry) {
                    entry->status = BUSY;
                    pthread_mutex_unlock(&mutex);
                    return entry;
                }

                if (!block || shutting_down) {
                    pthread_mutex_unlock(&mutex);
                    return 0;
                }

                if (timeout_ms > 0) {
                    struct timespec tm;
                    clock_gettime(CLOCK_REALTIME, &tm);
                    tm.tv_sec += timeout_ms / 1000;
                    tm.tv_nsec += (timeout_ms % 1000) * 1000000L;
                    if (tm.tv_nsec >= 1000000000L) {
                        tm.tv_sec += 1;
                        tm.tv_nsec -= 1000000000L;
                    }

                    if (pthread_cond_timedwait(&available_condition, &mutex, &tm) != 0) {
                        pthread_mutex_unlock(&mutex);
                        return 0;
                    }
                } else {
                    pthread_cond_wait(&available_condition, &mutex);
                }
            }
        }

        const T &getInstance(HANDLE handle) const {
            return ((ENTRY *)handle)->inst;
        }

        void release(HANDLE handle) {
            ENTRY *entry = (ENTRY *)handle;
            if (!entry)
                throw "Invalid Handle";

            pthread_mutex_lock(&mutex);

            if (entry->status != BUSY) {
                pthread_mutex_unlock(&mutex);
                throw "Invalid State";
            }

            entry->status = WAIT;

            if (entry->retired || shutting_down) {
                removeEntry(entry);
                destroyEntry(entry);
            } else {
                available.add(entry);
                pthread_cond_signal(&available_condition);
            }

            pthread_mutex_unlock(&mutex);
        }

        void autoFree(bool auto_free) {
            pthread_mutex_lock(&mutex);
            this->auto_free = auto_free;
            pthread_mutex_unlock(&mutex);
        }

        T get(int index) {
            pthread_mutex_lock(&mutex);
            if (index < 0 || index >= entries.length()) {
                pthread_mutex_unlock(&mutex);
                throw "Invalid Index";
            }

            LINKEDLIST_NODE_HANDLE node = entries.firstNode();
            while (index--)
                node = entries.nextNode(node);

            T value = entries.get(node)->inst;
            pthread_mutex_unlock(&mutex);
            return value;
        }

    private:
        LinkedList<ENTRY *> entries;
        LinkedList<ENTRY *> available;
        pthread_mutex_t mutex;
        pthread_cond_t available_condition;
        bool auto_free;
        bool shutting_down;

        ENTRY *popAvailableEntry() {
            if (!available.length())
                return 0;

            ENTRY *entry = available.get(available.firstNode());
            available.removeNode(available.firstNode());
            return entry;
        }

        void removeAvailableEntry(ENTRY *entry) {
            LINKEDLIST_NODE_HANDLE node = available.firstNode();
            if (!node)
                return;

            do {
                LINKEDLIST_NODE_HANDLE current = node;
                node = available.nextNode(current);
                if (available.get(current) == entry) {
                    available.removeNode(current);
                    return;
                }
            } while (node && node != available.firstNode());
        }

        void removeEntry(ENTRY *entry) {
            LINKEDLIST_NODE_HANDLE node = entries.firstNode();
            if (!node)
                return;

            do {
                LINKEDLIST_NODE_HANDLE current = node;
                node = entries.nextNode(current);
                if (entries.get(current) == entry) {
                    entries.removeNode(current);
                    return;
                }
            } while (node && node != entries.firstNode());
        }

        void destroyEntry(ENTRY *entry) {
            if (!entry)
                return;

            destroyInstance(entry->inst, typename std::is_pointer<T>::type());
            delete entry;
        }

        void destroyInstance(T &inst, std::true_type) {
            if (auto_free && inst) {
                delete inst;
                inst = 0;
            }
        }

        void destroyInstance(T &, std::false_type) {
        }
    };

}
}
}

namespace elara {
namespace threading {

template <class T>
using InstancePool = ::elara::threading::memory::InstancePool<T>;

}
}

#endif /* defined(__libElaraThreads__memory__InstancePool__) */

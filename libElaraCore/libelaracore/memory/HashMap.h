//
//  HashMap.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 24/08/2013.
//  Copyright (c) 2013 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__HashMap__
#define __libElaraCore__HashMap__

#include "Array.h"

#include "Memory.h"
#include "ByteArray.h"
#include "String.h"
#include "LinkedList.h"

namespace elara {
    
    template <class T>
    class HashMap {
    public:
        typedef struct {
            Memory key;
            Ref<T> obj;
        } MAPENTRY;
        
    public:
        HashMap() {
            parent = 0;
            key = 0;
            obj = Ref<T>();
        }
        
        virtual ~HashMap() {
            clear();
        }
        
        const char getKey() const {
            return key;
        }
        
        void set(Memory map_key, Ref<T> newobj) {
            HashMap<T> *hm = this, *pm = this;
            int i, key_len = (int)map_key.length();
            for (i=0; i<key_len; i++) {
                hm = hm->getHashMap(map_key[i]);
                if (!hm) {
                    hm = new HashMap<T>(pm, map_key[i]);
                    pm->map.push(hm);
                }
                pm = hm;
            }
            hm->setObject(newobj);
        }
        
        void set(String map_key, T newobj) {
            set(Memory(map_key.operator char *(), map_key.length()), Ref<T>(new T(newobj)));
        }
        
        Ref<T> get(Memory &map_key) const {
            const HashMap<T> *hm = this;
            int i, key_len = (int)map_key.length();
            for (i=0; i<key_len; i++) {
                hm = hm->getHashMap(map_key[i]);
                if (!hm)
                    break;
            }
            
            if (i==key_len)
                return hm->getObject();
            
            return Ref<T>();
        }
        
        void remove(Memory &map_key) {
            HashMap<T> *hm = this, *cm;
            int i, key_len = (int)map_key.length();
            for (i=0; i<key_len; i++) {
                hm = hm->getHashMap(map_key[i]);
                if (!hm)
                    break;
            }
            
            if (i==key_len) {
                hm->setObject(Ref<T>());
                
                while (hm && !hm->getMapSize() && !hm->getObject().getPtr()) {
                    cm = hm;
                    hm = hm->getParent();
                    if (!hm)
                        break;

                    hm->removeMap(cm->getKey());
                    delete cm;
                }
            }
        }
        
        void clear() {
            int len = (int)map.length();
            for (int i=0; i<len; i++) {
                map.get(i)->clear();
                delete map.get(i);
            }
            map.clear();
        }
        
        LinkedList< Ref<MAPENTRY> > getEntries(Memory key_prefix) const {
            LinkedList< Ref<MAPENTRY> > ret;
            
            const char k = getKey();
            ByteArray key(key_prefix.operator char *(), (int)key_prefix.length());
            if (k)
                key.append(&k, 1);
            
            if (obj.getPtr()) {
                Ref<MAPENTRY> cur_obj = Ref<MAPENTRY>(new MAPENTRY);
                cur_obj.getPtr()->key = Memory(key);
                cur_obj.getPtr()->obj = obj;
                ret.add(cur_obj);
            }
            
            ssize_t len = map.length();
            for (int i=0; i<len; i++) {
                ret.append(map.get(i)->getEntries(key));
            }
            
            return ret;
        }
        
        LinkedList< Ref<T> > getObjects(Memory key_prefix) const {
            LinkedList< Ref<T> > ret;
            
            const char k = getKey();
            ByteArray key(key_prefix.operator char *(), (int)key_prefix.length());
            if (k)
                key.append(&k, 1);
            
            if (obj.getPtr())
                ret.add(obj);
            
            ssize_t len = map.length();
            for (int i=0; i<len; i++) {
                ret.append(map.get(i)->getObjects(key));
            }
            
            return ret;
        }
        
    protected:
        HashMap(HashMap<T> *parent, const char map_key) {
            this->parent = parent;
            key = map_key;
            obj = Ref<T>();
        }
        
        const HashMap *getHashMap(const char key) const {
            int map_len = (int)map.length();
            for (int i=0; i<map_len; i++) {
                HashMap<T>* hm = map.get(i);
                if (hm && hm->getKey() == key)
                    return map.get(i);
            }
            return 0;
        }

        HashMap *getHashMap(const char key) {
            int map_len = (int)map.length();
            for (int i=0; i<map_len; i++) {
                HashMap<T>* hm = map.get(i);
                if (hm && hm->getKey() == key)
                    return map.get(i);
            }
            return 0;
        }
        
        void setObject(Ref<T> obj) {
            this->obj = obj;
        }
        
        Ref<T> getObject() const {
            return obj;
        }
        
        void pushMap(const char* key, HashMap<T>* map) {
            this->map.push(map);
        }
        
        void removeMap(const char key) {
            int map_len = (int)map.length();
            for (int i=0; i<map_len; i++) {
                HashMap<T>* hm = map.get(i);
                if (hm && hm->getKey() == key)
                    map.remove(i);
            }
        }
        
        int getMapSize() const {
            return (int)map.length();
        }
        
        HashMap<T> *getParent() {
            return parent;
        }
        
    private:
        HashMap<T> *parent;
        Array<HashMap<T>*> map;
        char    key;
        Ref<T>  obj;
    };
    
};

#endif /* defined(__libElaraCore__HashMap__) */

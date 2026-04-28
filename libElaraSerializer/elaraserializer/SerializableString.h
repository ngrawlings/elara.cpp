//
//  SerializableString.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 22/03/2014.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__SerializableString__
#define __libElaraCore__SerializableString__

#include <libelaracore/memory/String.h>
#include "Serializable.h"

namespace nrcore {
  
    class SerializableString : public String, public Serializable {
    public:
        SerializableString() {}
        SerializableString(const char *str);
        SerializableString(const String &str);
        SerializableString(const char c);
        
        SerializableString(int num);
        SerializableString(unsigned int num);
        
        SerializableString(long num);
        SerializableString(unsigned long num);
        
        SerializableString(long long num);
        SerializableString(unsigned long long num);
        
        SerializableString(double num);
        SerializableString(long double num);
        
        virtual ~SerializableString();
        
    protected:
        void beforeSerialization();
        void serializedObjectLoaded(int index, SERIAL_OBJECT *so);
    };
    
};

#endif /* defined(__libElaraCore__SerializableString__) */

//
//  Cipher.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 06/11/2012.
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
// For affordable commercial licensing please contact nyhl@ngrawlings.com
//

#ifndef PeerConnector_Cipher_h
#define PeerConnector_Cipher_h

#define ERROR_INVALID_KEY      1

#include <libelaracore/memory/Memory.h>

namespace elara {

    class Cipher {
    public:
        virtual ~Cipher() {}

        virtual void setKey(const Memory &key, const Memory &iv) {};
        
        virtual Memory encrypt(const char* buf, int len) = 0;
        virtual Memory decrypt(const char* buf, int len) = 0;
        
        virtual int getBlockSize() = 0;
        
    protected:
        Memory pad(const char* bytes, int len, int block_size);
        int unpaddedLength(const char* bytes, int len);
    };
    
};

#endif

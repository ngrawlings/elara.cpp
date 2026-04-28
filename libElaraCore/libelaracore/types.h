//
//  types.h
//  libElaraCore
//
//  Created by Nyhl Rawlings on 10/16/13.
//  Copyright (c) 2013 N G Rawlings. All rights reserved.
//

#ifndef libElaraCore_types_h
#define libElaraCore_types_h

#include <libelaracore/config.h>

#ifndef WIN32

#include <unistd.h>

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define ssize_t         SSIZE_T

#endif

#define thread_t        pthread_t
#define thread_mutex_t  pthread_mutex_t
#define thread_cond_t   pthread_cond_t
#define thread_key_t    pthread_key_t

#endif

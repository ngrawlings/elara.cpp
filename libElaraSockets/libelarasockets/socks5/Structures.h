//
//  Structures.h
//  LibElaraSockets
//
//  Created by Nyhl Rawlings on 19/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Structures_h
#define Structures_h

using namespace nrcore;

typedef enum {
    NO_AUTH             = 0,
    GSSAPI              = 1,
    USERNAME_PASSWORD   = 2
} AUTH_METHOD;

typedef enum {
    CONNECT         = 1,
    BIND            = 2,
    UDP_ASSOCIATE   = 3
} REQUEST_COMMAND;

typedef struct {
    unsigned char version;
    unsigned char auth_method_cnt;
    unsigned char auth_methouds[];
} CLIENT_INIT;

typedef struct {
    unsigned char version;
    unsigned char auth_method;
} SERVER_AUTH_SELECT;

typedef enum {
    IPV4                    = 1,
    DOMAIN                  = 3,
    IPV6                    = 4
} ADDRESS_TYPE;

typedef enum {
    SUCCESS                 = 0,
    GENERAL_FAILURE         = 1,
    NOT_ALLOWED             = 2,
    NETWORK_UNREACHABLE     = 3,
    HOST_UNREACHABLE        = 4,
    CONNECTION_REFUSED      = 5,
    TTL_EXPIRED             = 6,
    CMD_NOT_SUPPORTED       = 7,
    ADDRESS_NOT_SUPPORTED   = 8
} RESULT;

typedef struct {
    unsigned char version;
    unsigned char result;
} AUTH_RESULT;

typedef struct {
    unsigned char version;
    unsigned char cmd;
    unsigned char rsv;
    unsigned char address_type;
} CLIENT_REQUEST;

typedef struct {
    unsigned char version;
    unsigned char result;
    unsigned char reserved;
    unsigned char address_type;
} SERVER_RESPONSE;

typedef struct {
    unsigned char ip[4];
    unsigned short port;
} ADDRESS_IPV4;

typedef struct {
    unsigned char length;
    unsigned char domain[];
} ADDRESS_DOMAIN;

typedef struct {
    unsigned char ip[16];
    unsigned short port;
} ADDRESS_IPV6;

typedef struct {
    unsigned char username_len;
    ByteArray username;
    unsigned char password_len;
    ByteArray password;
} AUTH_USERNAME_PASSWORD;


#endif /* Structures_h */

// SPDX-License-Identifier: MIT
#pragma once

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/types.h>

#include "ews.h"
#include "ews_config.h"


typedef enum ews_sock_flags ews_sock_flags_t;
typedef struct ews_sock_ops ews_sock_ops_t;
typedef struct ews_sock_evt ews_sock_evt_t;
typedef struct ews_sock ews_sock_t;

enum ews_sock_flags {
    EWS_SOCK_FLAG_TYPE_MASK         = 15 <<  0,
    EWS_SOCK_FLAG_TYPE_LISTEN       =  0 <<  0,
    EWS_SOCK_FLAG_TYPE_CLIENT       =  8 <<  0,

    EWS_SOCK_FLAG_PROTO_MASK        = 15 <<  4,
    EWS_SOCK_FLAG_PROTO_HTTP        =  0 <<  4,
    EWS_SOCK_FLAG_PROTO_WEBSOCKET   =  1 <<  4,
    EWS_SOCK_FLAG_PROTO_HTTP2       =  2 <<  4,

    EWS_SOCK_FLAG_INUSE             =  1 <<  8,
    EWS_SOCK_FLAG_TLS               =  1 <<  9,
    EWS_SOCK_FLAG_CONNECTED         =  1 << 10,
    EWS_SOCK_FLAG_SHUTDOWN          =  1 << 11,
    EWS_SOCK_FLAG_PEND_CLOSE        =  1 << 12,
};

struct ews_sock_ops {
    ssize_t (*send)(ews_sock_t *sock, const void *buf, size_t len);
    ssize_t (*recv)(ews_sock_t *sock, void *buf, size_t len);
    size_t (*avail)(ews_sock_t *sock);
    void (*set_block)(ews_sock_t *sock, bool block);
    void (*shutdown)(ews_sock_t *sock);
    void (*close)(ews_sock_t *sock);
};

struct ews_sock_evt {
    void (*on_connect)(ews_sock_t *sock);
    void (*on_close)(ews_sock_t *sock);
    bool (*want_read)(ews_sock_t *sock);
    bool (*want_write)(ews_sock_t *sock);
    void (*do_read)(ews_sock_t *sock);
    void (*do_write)(ews_sock_t *sock);
};

struct ews_sock {
    ews_t *ews;
    int fd;
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
#if CONFIG_EWS_USE_IPV6
        struct sockaddr_in6 in6;
#endif
    };
    void (*connect)(ews_sock_t *sock);
    const ews_sock_ops_t *ops;
    const ews_sock_evt_t *evt;
    ews_sock_flags_t flags;
    uint32_t last_active;
    uint32_t idle_timeout;
    void *user;
};

void ews_connect(ews_sock_t *sock);
void ews_connect_tls(ews_sock_t *sock);

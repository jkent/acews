// SPDX-License-Identifier: MIT
#pragma once

#include <stdarg.h>
#include <stdlib.h>

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#include "ews.h"
#include "route.h"
#include "socket.h"


typedef struct ews_http_conn ews_http_conn_t;

/// http connection data
struct ews_http_conn {
    ews_sock_t *sock;
    char buf[CONFIG_EWS_SESSION_BUFSIZE];
    size_t bufpos;
    size_t buflen;
};

typedef struct ews_req ews_req_t;

/// http request data
struct ews_req {
    const char *method;
    size_t method_len;
    const char *path;
    size_t path_len;
    const char *headers;
    const char *hdr_name;

    const char *x_path;
    size_t x_path_len;
    const char *x_query;
    size_t x_query_len;

    ssize_t length, consumed;

    const char *boundary;
    int boundary_len;
};

typedef struct ews_rsp ews_rsp_t;

/// http response data
struct ews_rsp {
    ssize_t length, produced;
};

enum {
    EWS_FLAGS_KEEPALIVE     = (1 <<  8),
    EWS_FLAGS_RSP_CHUNKED   = (1 <<  9),
    EWS_FLAGS_RSP_STARTED   = (1 << 10),
    EWS_FLAGS_WEBSOCKET     = (1 << 11),
};

typedef struct ews_sess ews_sess_t;

/// http session (private view)
struct ews_sess {
    ews_state_t state;
    ews_flags_t flags;
    int state_count;
    ews_req_t _req;
    ews_rsp_t _rsp;
};

/// http instance (private view)
struct ews_http {
    const ews_http_ops_t *ops;
    const ews_route_t *route;
    ews_sess_t *sess;
    void *user;
    ews_http_conn_t _conn;
    ews_sess_t _sess;
};

/// http socket event instance
extern const ews_sock_evt_t http_sock_evt;

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
    int bufpos;
    int buflen;
};

typedef struct ews_http_req ews_http_req_t;

/// http request data
struct ews_http_req {
    const char *method;
    const char *path;
    const char *headers;
    const char *hdr_name;

    const char *x_path;
    const char *x_query;

    int length, consumed;

    const char *boundary;
    int boundary_len;
};

typedef struct ews_http_rsp ews_http_rsp_t;

/// http response data
struct ews_http_rsp {
    int length, produced;
};

#define EWS_FLAGS_KEEPALIVE         (1 <<  8)
#define EWS_FLAGS_RSP_CHUNKED       (1 <<  9)
#define EWS_FLAGS_RSP_STARTED       (1 << 10)

typedef struct ews_http_sess ews_http_sess_t;

/// http session data (private view)
struct ews_http_sess {
    const ews_route_t *route;
    ews_state_t state;
    ews_http_flags_t flags;
    int state_count;
    ews_http_req_t req;
    ews_http_rsp_t rsp;
};

/// http session instance (private view)
struct ews_http {
    const ews_http_ops_t *ops;
    ews_http_conn_t conn;
    ews_http_sess_t sess;
};

/// http socket event instance
extern const ews_sock_evt_t http_sock_evt;

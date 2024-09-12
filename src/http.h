// SPDX-License-Identifier: MIT
#pragma once

#include <stdarg.h>
#include <stdlib.h>

#include "ews_config.h"
#include "route.h"
#include "socket.h"


/// http version type
typedef enum ews_http_version ews_http_version_t;

/// http flags type
typedef enum ews_http_flags ews_http_flags_t;

/// http request type
typedef struct ews_http_request ews_http_request_t;

/// http response type
typedef struct ews_http_response ews_http_response_t;

/// http data type
typedef struct ews_http_data ews_http_data_t;

/// http version enum
enum ews_http_version {
    EWS_HTTP_VERSION_09,
    EWS_HTTP_VERSION_10,
    EWS_HTTP_VERSION_11,
};

/// http flags enum
enum ews_http_flags {
    EWS_HTTP_FLAGS_FINALIZED            =  1 <<  0,
    EWS_HTTP_FLAGS_KEEPALIVE            =  1 <<  1,
    EWS_HTTP_FLAGS_REQUEST_CHUNKED      =  1 <<  2,
    EWS_HTTP_FLAGS_REQUEST_MULTIPART    =  1 <<  3,
    EWS_HTTP_FLAGS_RESPONSE_CHUNKED     =  1 <<  4,
};

/// http request struct
struct ews_http_request {
    uint8_t *buf;
    ssize_t buflen;
};

/// http response struct
struct ews_http_response {
    size_t length;
};

/// http data struct
struct ews_http_data {
    uint8_t buf[CONFIG_EWS_SESSION_BUFSIZE];
    size_t bufpos;
    size_t buflen;

    ews_sess_t sess;

    struct {
        uint8_t version;
        const ews_route_t *route;
        uint8_t state, prev_state, flags;
        size_t state_count;

        ews_http_request_t request;
        ews_http_response_t response;
    } block;
};

/// http socket event instance
extern const ews_sock_evt_t http_sock_evt;

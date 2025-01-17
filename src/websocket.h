// SPDX-License-Identifier: MIT
#pragma once

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#include "ews.h"
#include "route.h"

#if CONFIG_EWS_WS_CLIENTS > 0 || CONFIG_EWS_WSS_CLIENTS > 0

typedef struct ews_ws_send ews_ws_send_t;
struct ews_ws_send {
    int state;
};

typedef struct ews_ws_recv ews_ws_recv_t;
struct ews_ws_recv {
    int state;
    uint8_t opcode;
    uint32_t length, consumed;
    uint8_t key[4];
};

extern const ews_ws_ops_t ws_ops;

/// ws session struct (private view)
struct ews_ws {
    const ews_ws_ops_t *ops;
    const ews_route_t *route;
    void *user;
    ews_sock_t *_sock;
    ews_ws_send_t _send;
    ews_ws_recv_t _recv;
};

/// perform a websocket upgrade on an http connection
/// @param[inout] http session instance
/// @param[in] http_sock http socket
int ews_ws_upgrade(ews_http_t *http, ews_sock_t *http_sock);

#endif

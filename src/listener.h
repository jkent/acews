// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include "ews_config.h"
#include "socket.h"


typedef struct ews_listener ews_listener_t;

struct ews_listener {
    ews_sock_t sock;
};

bool listener_init(ews_t *ews, ews_listener_t *listener, uint16_t port,
        int backlog, bool tls);

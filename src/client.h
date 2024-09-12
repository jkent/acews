// SPDX-License-Identifier: MIT
#pragma once

#include "ews_config.h"

#if CONFIG_EWS_HTTPS_CLIENTS > 0
# include <mbedtls/ssl.h>
#endif

#include "ews_port.h"
#include "socket.h"


typedef struct ews_client ews_client_t;
typedef struct ews_client_tls ews_client_tls_t;

struct ews_client {
    ews_sock_t sock;
};

#if CONFIG_EWS_HTTPS_CLIENTS > 0
struct ews_client_tls {
    ews_sock_t sock;
    ews_thread_t thread;
    mbedtls_ssl_context ssl_ctx;
};
#endif

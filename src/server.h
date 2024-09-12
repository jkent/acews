// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ews_config.h"

#if CONFIG_EWS_HTTPS_CLIENTS > 0
# include <mbedtls/ctr_drbg.h>
# include <mbedtls/entropy.h>
# include <mbedtls/ssl.h>
# include <mbedtls/x509.h>
#endif

#include "client.h"
#include "ews.h"
#include "ews_port.h"
#include "listener.h"
#include "route.h"
#include "worker.h"


typedef struct ews ews_t;

struct ews {
    ews_mutex_t mutex;
    ews_config_t config;

#if CONFIG_EWS_HTTP_CLIENTS > 0 || defined(__DOXYGEN__)
    ews_listener_t http_listener;
    ews_client_t http_client[CONFIG_EWS_HTTP_CLIENTS];
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0 || defined(__DOXYGEN__)
    struct {
        mbedtls_ssl_config ssl_cfg;
        mbedtls_x509_crt x509_crt;
        mbedtls_pk_context pk_ctx;
        mbedtls_entropy_context entropy_ctx;
        mbedtls_ctr_drbg_context drbg_ctx;
    } tls;

    ews_listener_t https_listener;
    ews_client_tls_t https_client[CONFIG_EWS_HTTPS_CLIENTS];
#endif

    ews_route_t *route_first;
    ews_route_t *route_last;

    ews_worker_t worker;
};

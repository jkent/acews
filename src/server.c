// SPDX-License-Identifier: MIT
#include <string.h>

#include "ews_config.h"

#if CONFIG_EWS_HTTPS_CLIENTS > 0
# include <mbedtls/ctr_drbg.h>
# include <mbedtls/entropy.h>
# include <mbedtls/error.h>
# include <mbedtls/ssl.h>
# include <mbedtls/x509.h>
#endif

#include "server.h"
#include "ews_port.h"
#include "listener.h"


ews_t *ews_init(const ews_config_t *config)
{
    ews_t *ews;

    ews = calloc(1, sizeof(*ews));
    if (ews == NULL) {
        LOGE("calloc failed");
        return NULL;
    }

    ews_mutex_init(&ews->mutex, true);

    if (config) {
        memcpy(&ews->config, config, sizeof(ews->config));
    }

    if (ews->config.idle_timeout <= 0) {
        ews->config.idle_timeout = CONFIG_EWS_IDLE_TIMEOUT_DFLT;
    }

#if CONFIG_EWS_HTTP_CLIENTS > 0
    if (ews->config.http_listen_port <= 0) {
        ews->config.http_listen_port = 80;
    }
    if (config && ews->config.http_listen_backlog < 0) {
        ews->config.http_listen_backlog = 0;
    } else if (ews->config.http_listen_backlog == 0) {
        ews->config.http_listen_backlog = CONFIG_EWS_HTTP_BACKLOG_DFLT;
    }

    /// initialize http listener
    listener_init(ews, &ews->http_listener, ews->config.http_listen_port,
            ews->config.http_listen_backlog, false);
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0
    if (ews->config.https_listen_port <= 0) {
        ews->config.https_listen_port = 443;
    }
    if (config && ews->config.https_listen_backlog < 0) {
        ews->config.https_listen_backlog = 0;
    } else if (ews->config.https_listen_backlog == 0) {
        ews->config.https_listen_backlog = CONFIG_EWS_HTTPS_BACKLOG_DFLT;
    }

    if (ews->config.https_crt) {
        int ret;

        mbedtls_ssl_config_init(&ews->tls.ssl_cfg);
        mbedtls_x509_crt_init(&ews->tls.x509_crt);
        mbedtls_pk_init(&ews->tls.pk_ctx);
        mbedtls_entropy_init(&ews->tls.entropy_ctx);
        mbedtls_ctr_drbg_init(&ews->tls.drbg_ctx);

        ret = mbedtls_ctr_drbg_seed(&ews->tls.drbg_ctx, mbedtls_entropy_func,
                &ews->tls.entropy_ctx, NULL, 0);
        if (ret < 0) {
            LOGE("mbedtls_ctr_drbg_seed failed");
            goto fail;
        }

        ret = mbedtls_ssl_config_defaults(&ews->tls.ssl_cfg,
                MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret < 0) {
            LOGE("mbedtls_ssl_cnofig_defaults failed");
            goto fail;
        }

        mbedtls_ssl_conf_rng(&ews->tls.ssl_cfg, mbedtls_ctr_drbg_random,
                &ews->tls.drbg_ctx);

        ret = mbedtls_x509_crt_parse(&ews->tls.x509_crt, ews->config.https_crt,
                ews->config.https_crt_len);
        if (ret < 0) {
# if LOG_LEVEL >= LOG_DEBUG
            char s[128];
            mbedtls_strerror(ret, s, sizeof(s));
            LOGE("mbedtls_x509_crt_parse failed: %s", s);
# else
            LOGE("mbedtls_x509_crt_parse failed");
# endif
            goto fail;
        }

# if MBEDTLS_VERSION_MAJOR < 3
        ret = mbedtls_pk_parse_key(&ews->tls.pk_ctx, ews->config.https_pk,
                ews->config.https_pk_len, NULL, 0);
# else
        ret = mbedtls_pk_parse_key(&ews->tls.pk_ctx, ews->config.https_pk,
                ews->config.https_pk_len, NULL, 0, mbedtls_ctr_drbg_random,
                NULL);
# endif
        if (ret < 0) {
# if LOG_LEVEL >= LOG_DEBUG
            char s[128];
            mbedtls_strerror(ret, s, sizeof(s));
            LOGE("mbedtls_pk_parse_key failed: %s", s);
# else
            LOGE("mbedtls_pk_parse_key failed");
# endif
            goto fail;
        }

        ret = mbedtls_ssl_conf_own_cert(&ews->tls.ssl_cfg, &ews->tls.x509_crt,
                &ews->tls.pk_ctx);
        if (ret < 0) {
            LOGE("mbedtls_ssl_conf_own_cert failed");
            goto fail;
        }

        /// initialize https listener
        listener_init(ews, &ews->https_listener, ews->config.https_listen_port,
                ews->config.https_listen_backlog, true);
    }
#endif

    /// initialize worker
    if (!ews_worker_init(&ews->worker)) {
        goto fail;
    }

    return ews;

fail:
#if CONFIG_EWS_HTTPS_CLIENTS > 0
    mbedtls_ctr_drbg_free(&ews->tls.drbg_ctx);
    mbedtls_entropy_free(&ews->tls.entropy_ctx);
    mbedtls_pk_free(&ews->tls.pk_ctx);
    mbedtls_x509_crt_free(&ews->tls.x509_crt);
    mbedtls_ssl_config_free(&ews->tls.ssl_cfg);
#endif

    ews_mutex_destroy(&ews->mutex);
    free(ews);
    return NULL;
}

void ews_destroy(ews_t *ews)
{
    assert(ews != NULL);

    ews_worker_destroy(&ews->worker);

    // ews_route_clear(ews);

#if CONFIG_EWS_HTTPS_CLIENTS > 0
    mbedtls_ctr_drbg_free(&ews->tls.drbg_ctx);
    mbedtls_entropy_free(&ews->tls.entropy_ctx);
    mbedtls_pk_free(&ews->tls.pk_ctx);
    mbedtls_x509_crt_free(&ews->tls.x509_crt);
    mbedtls_ssl_config_free(&ews->tls.ssl_cfg);
#endif

    ews_mutex_destroy(&ews->mutex);
    free(ews);
}

#if CONFIG_EWS_HTTPS_CLIENTS > 0 || defined(__DOXYGEN__)
bool ews_add_client_cert(ews_t *ews, const uint8_t *crt, size_t crt_len)
{
    int ret;

    assert(ews != NULL);

    ret = mbedtls_x509_crt_parse(&ews->tls.x509_crt, crt, crt_len);
    if (ret < 0) {
        LOGE("mbedtls_x509_crt_parse failed");
        return false;
    }

    mbedtls_ssl_conf_authmode(&ews->tls.ssl_cfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    return true;
}
#endif

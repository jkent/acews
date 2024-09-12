// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ews_config.h"

#if CONFIG_EWS_HTTPS_CLIENTS > 0
# include <mbedtls/net_sockets.h>
# include <mbedtls/ssl.h>
#endif

#include "socket.h"
#include "client.h"
#include "ews_port.h"
#include "http.h"
#include "log.h"
#include "server.h"


#if CONFIG_EWS_HTTP_CLIENTS > 0
static ssize_t ews_sock_send(ews_sock_t *sock, const void *buf, size_t len)
{
    ssize_t ret;

    if (sock->flags & EWS_SOCK_FLAG_SHUTDOWN) {
        return -1;
    }

    ret = send(sock->fd, buf, len, 0);
    if (ret < 0) {
        if (errno = ECONNRESET) {
            LOGI("connection reset by peer");
        } else if (errno == EAGAIN) {
            return -1;
        }
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return -1;
    }
    return ret;
}

static ssize_t ews_sock_recv(ews_sock_t *sock, void *buf, size_t len)
{
    ssize_t ret = recv(sock->fd, buf, len, 0);
    if (ret < 0) {
        if (errno == ECONNRESET) {
            LOGI("connection reset by peer");
        } else if (errno == EAGAIN) {
            return -1;
        }
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return -1;
    } else if (ret == 0) {
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
    }
    return ret;
}

static size_t ews_sock_avail(ews_sock_t *sock)
{
    return 0;
}

static void ews_sock_set_block(ews_sock_t *sock, bool block)
{
    if (block) {
        fcntl(sock->fd, F_SETFL, fcntl(sock->fd, F_GETFL) & ~O_NONBLOCK);
    } else {
        fcntl(sock->fd, F_SETFL, fcntl(sock->fd, F_GETFL) | O_NONBLOCK);
    }
}

static void ews_sock_shutdown(ews_sock_t *sock)
{
    LOGD("#%d shutdown", sock->fd);
    shutdown(sock->fd, SHUT_WR);
    sock->flags |= EWS_SOCK_FLAG_SHUTDOWN;
}

static void ews_sock_close(ews_sock_t *sock)
{
    LOGI("#%d close", sock->fd);
    close(sock->fd);
    memset(sock, 0, sizeof(*sock));
}

static const struct ews_sock_ops ews_sock_ops = {
    .send = ews_sock_send,
    .recv = ews_sock_recv,
    .avail = ews_sock_avail,
    .set_block = ews_sock_set_block,
    .shutdown = ews_sock_shutdown,
    .close = ews_sock_close,
};

void ews_connect(ews_sock_t *sock)
{
    sock->ops = &ews_sock_ops;

#if LOG_LEVEL >= LOG_INFO
    {
# if CONFIG_EWS_USE_IPV6
        char s[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sock->in6.sin6_addr, s, sizeof(s));
        LOGI("#%d connect [%s]:%hu", sock->fd, s, htons(sock->in6.sin6_port));
# else
        char s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sock->in.sin_addr, s, sizeof(s));
        LOGI("#%d connect %s:%hu", sock->fd, s, htons(sock->in.sin_port));
# endif
    }
#endif

    sock->idle_timeout = sock->ews->config.idle_timeout;
    sock->evt = &http_sock_evt;
}
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0
static ssize_t ews_sock_send_tls(ews_sock_t *sock, const void *buf, size_t len)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;
    int ret;

    if (sock->flags & EWS_SOCK_FLAG_SHUTDOWN) {
        return -1;
    }

    ret = mbedtls_ssl_write(&client->ssl_ctx, buf, len);
    if (ret < 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        } else if (ret == MBEDTLS_ERR_NET_CONN_RESET) {
            LOGI("connection reset by peer");
        }
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return -1;
    }
    return ret;
}

static ssize_t ews_sock_recv_tls(ews_sock_t *sock, void *buf, size_t len)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;
    int ret;

    ret = mbedtls_ssl_read(&client->ssl_ctx, buf, len);
    if (ret < 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        } else if (ret == MBEDTLS_ERR_NET_CONN_RESET) {
            LOGI("connection reset by peer");
        } else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            LOGI("peer notified us about closure");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
            return 0;
        }
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return -1;
    } else if (ret == 0) {
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
    }
    return ret;
}

static size_t ews_sock_avail_tls(ews_sock_t *sock)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;

    return mbedtls_ssl_get_bytes_avail(&client->ssl_ctx);
}

static void ews_sock_set_block_tls(ews_sock_t *sock, bool block)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;

    if (block) {
        mbedtls_net_set_block(client->ssl_ctx.p_bio);
    } else {
        mbedtls_net_set_nonblock(client->ssl_ctx.p_bio);
    }
}

static void ews_sock_shutdown_tls(ews_sock_t *sock)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;

    LOGD("#%d shutdown", sock->fd);
    mbedtls_ssl_close_notify(&client->ssl_ctx);
    sock->flags |= EWS_SOCK_FLAG_SHUTDOWN;
}

static void ews_sock_close_tls(ews_sock_t *sock)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;

    LOGI("#%d close", sock->fd);
    mbedtls_ssl_free(&client->ssl_ctx);
    close(sock->fd);
    memset(sock, 0, sizeof(*sock));
}

static const struct ews_sock_ops ews_tls_sock_ops = {
    .send = ews_sock_send_tls,
    .recv = ews_sock_recv_tls,
    .avail = ews_sock_avail_tls,
    .set_block = ews_sock_set_block_tls,
    .shutdown = ews_sock_shutdown_tls,
    .close = ews_sock_close_tls,
};

static void ews_connect_tls_task(void *arg)
{
    ews_sock_t *sock = (ews_sock_t *) arg;
    ews_client_tls_t *client = (ews_client_tls_t *) sock;
    int ret;

    mbedtls_ssl_init(&client->ssl_ctx);

    ret = mbedtls_ssl_setup(&client->ssl_ctx, &sock->ews->tls.ssl_cfg);
    if (ret < 0) {
        LOGE("mbedtls_ssl_setup failed");
        goto fail;
    }

    mbedtls_ssl_set_bio(&client->ssl_ctx, &sock->fd, mbedtls_net_send,
            mbedtls_net_recv, NULL);

    ret = mbedtls_ssl_handshake(&client->ssl_ctx);
    if (ret < 0) {
        LOGE("mbedtls_ssl_handshake failed");
        goto fail;
    }

    LOGV("#%d TLS handshake OK", sock->fd);

    sock->evt = &http_sock_evt;
    return;

fail:
    ews_sock_close_tls(sock);
}

void ews_connect_tls(ews_sock_t *sock)
{
    ews_client_tls_t *client = (ews_client_tls_t *) sock;
    sock->ops = &ews_tls_sock_ops;

#if LOG_LEVEL >= LOG_INFO
    {
# if CONFIG_EWS_USE_IPV6
        char s[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sock->in6.sin6_addr, s, sizeof(s));
        LOGI("#%d connect [%s]:%hu TLS", sock->fd, s,
                htons(sock->in6.sin6_port));
# else
        char s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sock->in.sin_addr, s, sizeof(s));
        LOGI("#%d connect %s:%hu TLS", sock->fd, s, htons(sock->in.sin_port));
# endif
    }
#endif

    sock->idle_timeout = sock->ews->config.idle_timeout;
    ews_thread_init(&client->thread, ews_connect_tls_task, sock, 1024);
}
#endif

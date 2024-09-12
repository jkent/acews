// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#include "listener.h"
#include "client.h"
#include "ews_config.h"
#include "ews_port.h"
#include "server.h"
#include "socket.h"


static const ews_sock_evt_t listener_sock_evt;

bool listener_init(ews_t *ews, ews_listener_t *listener, uint16_t port,
        int backlog, bool tls)
{
    ews_sock_t *sock = &listener->sock;
    socklen_t socklen;
    int ret;

    sock->flags |= EWS_SOCK_FLAG_INUSE | EWS_SOCK_FLAG_TYPE_LISTEN;

#if CONFIG_EWS_HTTP_CLIENTS > 0
    if (tls) {
        listener->sock.flags |= EWS_SOCK_FLAG_TLS;
    }
#endif

#if CONFIG_EWS_USE_IPV6
    sock->in6.sin6_family = AF_INET6;
    sock->in6.sin6_addr = in6addr_any;
    sock->in6.sin6_port = ntohs(port);
#else
    sock->in.sin_family = AF_INET;
    sock->in.sin_addr.s_addr = INADDR_ANY;
    sock->in.sin_port = ntohs(port);
#endif

    sock->fd = socket(sock->sa.sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock->fd < 0) {
        LOGE("socket failed");
        goto fail;
    }

#if CONFIG_EWS_USE_IPV6
    setsockopt(sock->fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int));
#endif
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

#if CONFIG_EWS_USE_IPV6
    socklen = sizeof(struct sockaddr_in6);
# if LOG_LEVEL >= LOG_INFO
   char s[INET6_ADDRSTRLEN];
   inet_ntop(sock->sa.sa_family, &sock->in6.sin6_addr, s, sizeof(s));
   LOGI("#%d bind [%s]:%hu%s", sock->fd, s, ntohs(sock->in6.sin6_port),
        tls ? " TLS" : "");
# endif
#else
    socklen = sizeof(struct sockaddr_in);
# if LOG_LEVEL >= LOG_INFO
   char s[INET_ADDRSTRLEN];
   inet_ntop(sock->sa.sa_family, &sock->in.sin_addr, s, sizeof(s));
   LOGI("#%d bind %s:%hu%s", sock->fd, s, ntohs(sock->in.sin_port),
        tls ? " TLS" : "");
# endif
#endif

    ret = bind(sock->fd, &sock->sa, socklen);
    if (ret < 0) {
        LOGE("bind failed");
        goto fail;
    }

    ret = listen(sock->fd, backlog);
    if (ret < 0) {
        LOGE("listen failed");
        goto fail;
    }

    sock->ews = ews;
    sock->evt = &listener_sock_evt;
    sock->flags |= EWS_SOCK_FLAG_CONNECTED;

    return true;

fail:
    sock->flags = 0;
    if (sock->fd > 0) {
        close(sock->fd);
        sock->fd = -1;
    }
    return false;
}

static void on_close(ews_sock_t *sock)
{
    close(sock->fd);
    sock->flags &= ~EWS_SOCK_FLAG_CONNECTED;
}

static bool want_read(ews_sock_t *sock)
{
    return true;
}

static void do_read(ews_sock_t *sock)
{
    ews_t *ews = sock->ews;
    ews_sock_t *client_sock = NULL;

    ews_mutex_lock(&ews->mutex);

#if CONFIG_EWS_HTTP_CLIENTS > 0
    if (!(sock->flags & EWS_SOCK_FLAG_TLS)) {
        ews_client_t *client;
        for (int i = 0; i < countof(ews->http_client); i++) {
            client = &ews->http_client[i];
            if (!(client->sock.flags & EWS_SOCK_FLAG_INUSE)) {
                memset(client, 0, sizeof(*client));
                client_sock = &client->sock;
                break;
            }
        }
    }
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0
    if (sock->flags & EWS_SOCK_FLAG_TLS) {
        ews_client_tls_t *client;
        for (int i = 0; i < countof(ews->https_client); i++) {
            client = &ews->https_client[i];
            if (!(client->sock.flags & EWS_SOCK_FLAG_INUSE)) {
                memset(client, 0, sizeof(*client));
                client_sock = &client->sock;
                break;
            }
        }
    }
#endif

    if (client_sock) {
#if CONFIG_EWS_USE_IPV6
        socklen_t socklen = sizeof(struct sockaddr_in6);
#else
        socklen_t socklen = sizeof(struct sockaddr_in);
#endif
        client_sock->fd = accept(sock->fd, &client_sock->sa, &socklen);
        if (client_sock->fd < 0) {
            return;
        }

        client_sock->ews = ews;
        client_sock->last_active = ews_time_ms();
        client_sock->flags |= EWS_SOCK_FLAG_INUSE | EWS_SOCK_FLAG_TYPE_CLIENT;
#if CONFIG_EWS_HTTP_CLIENTS > 0
        if (!(sock->flags & EWS_SOCK_FLAG_TLS)) {
            client_sock->connect = ews_connect;
        }
#endif
#if CONFIG_EWS_HTTPS_CLIENTS > 0
        if (sock->flags & EWS_SOCK_FLAG_TLS) {
            client_sock->flags |= EWS_SOCK_FLAG_TLS;
            client_sock->connect = ews_connect_tls;
        }
#endif
    }

    ews_mutex_unlock(&ews->mutex);
}

static const ews_sock_evt_t listener_sock_evt = {
    .on_close = on_close,
    .want_read = want_read,
    .do_read = do_read,
};

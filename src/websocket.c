// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>

#include "ews_config.h"

#if CONFIG_EWS_WS_CLIENTS > 0 || CONFIG_EWS_WSS_CLIENTS > 0
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>


#define EWS_PRIVATE_DEFS
#include "ews.h"
#include "ews_port.h"
#include "http.h"
#include "server.h"
#include "socket.h"
#include "utils.h"
#include "websocket.h"


enum {
    STATE_TYPE_NONE     = (0 <<  0),
    STATE_TYPE_TEXT     = (1 <<  0),
    STATE_TYPE_BINARY   = (2 <<  0),
    STATE_TYPE_MASK     = (3 <<  0),
};

enum {
    OPCODE_CONT     = 0x0,
    OPCODE_TEXT     = 0x1,
    OPCODE_BIN      = 0x2,
    OPCODE_CLOSE    = 0x8,
    OPCODE_PING     = 0x9,
    OPCODE_PONG     = 0xA,
};

#define FLAG_FIN        0x80

int ews_ws_gen_accept(char *out, size_t out_len, const char *key)
{
    unsigned char s[61], buf[20];
    size_t len;
    int ret;

    len = strlen(key);
    if (len != 24) {
        return -1;
    }

    len = sprintf((char *) s, "%.*s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
            (int) len, key);
    if (len != 60) {
        return -1;
    }

    ret = mbedtls_sha1_ret(s, len, buf);
    if (ret != 0) {
        return -1;
    }

    ret = mbedtls_base64_encode((unsigned char *) out, out_len, &len, buf,
            sizeof(buf));
    if (ret != 0) {
        return -1;
    }

    return 0;
}

static int ws_send_hdr(ews_ws_t *ws, int opcode, size_t len)
{
    ews_sock_t *sock = ws->_sock;
    uint8_t buf[10];
    size_t i = 0;

    buf[i++] = opcode;
    if (len >= 65536) {
        buf[i++] = 127;
        buf[i++] = 0;
        buf[i++] = 0;
        buf[i++] = 0;
        buf[i++] = 0;
        buf[i++] = len >> 24;
        buf[i++] = len >> 16;
        buf[i++] = len >> 8;
        buf[i++] = len;
    } else if (len >= 126) {
        buf[i++] = 126;
        buf[i++] = len >> 8;
        buf[i++] = len;
    } else {
        buf[i++] = len;
    }

    if (sock->ops->send(sock, buf, i) < 0) {
        return -1;
    }

    return 0;
}

static int ws_recv_hdr(ews_ws_t *ws)
{
    ews_sock_t *sock = ws->_sock;
    char hdr[10], buf[125];
    int ret;

    while (true) {
        ret = sock->ops->recv(sock, hdr, 2);
        if (ret != 2) {
            return -1;
        }

        ws->_recv.opcode = hdr[0];
        if ((hdr[1] & 0x7F) < 126) {
            ws->_recv.length = hdr[1] & 0x7F;
        } else if ((hdr[1] & 0x7F) == 126) {
            ret = sock->ops->recv(sock, &hdr[2], 2);
            if (ret != 2) {
                return -1;
            }
            ws->_recv.length = hdr[2] << 8;
            ws->_recv.length |= hdr[3];
        } else {
            ret = sock->ops->recv(sock, &hdr[2], 8);
            if (ret != 8) {
                return -1;
            }
            if (hdr[2] != 0 || hdr[3] != 0 || hdr[4] != 0 || hdr[5] != 0) {
                return -1;
            }
            ws->_recv.length = hdr[6] << 24;
            ws->_recv.length |= hdr[7] << 16;
            ws->_recv.length |= hdr[8] << 8;
            ws->_recv.length |= hdr[9];
        }

        if (hdr[1] & 0x80) {
            ret = sock->ops->recv(sock, ws->_recv.key, 4);
            if (ret != 4) {
                return -1;
            }
        } else {
            memset(ws->_recv.key, 0, 4);
        }

        switch (hdr[0] & 0xf) {
        case OPCODE_CONT:
            if ((ws->_recv.state & STATE_TYPE_MASK) == STATE_TYPE_NONE) {
                return -1;
            }
            return 0;

        case OPCODE_TEXT:
            if ((ws->_recv.state & STATE_TYPE_MASK) != STATE_TYPE_NONE) {
                return -1;
            }
            ws->_recv.state |= STATE_TYPE_TEXT;
            return 0;

        case OPCODE_BIN:
            if ((ws->_recv.state & STATE_TYPE_MASK) != STATE_TYPE_NONE) {
                return -1;
            }
            ws->_recv.state |= STATE_TYPE_BINARY;
            return 0;

        case OPCODE_CLOSE:
            ws_send_hdr(ws, OPCODE_CLOSE, 0);
            return -1;

        case OPCODE_PING:
            if (ws->_recv.length > sizeof(buf)) {
                return -1;
            }
            ret = sock->ops->recv(sock, buf, ws->_recv.length);
            if (ret != ws->_recv.length) {
                return -1;
            }
            for (int i = 0; i < ret; i++) {
                buf[i] ^= ws->_recv.key[i % 4];
            }
            if (ws_send_hdr(ws, OPCODE_PONG | FLAG_FIN, ret) < 0) {
                return -1;
            }
            if (sock->ops->send(sock, buf, ret) < 0) {
                return -1;
            }
            break;

        default:
            return -1;
        }
    }

    return 0;
}

static ssize_t ws_ops_recv(ews_ws_t *ws, int *flags, char *buf, ssize_t buf_sz)
{
    size_t remaining = ws->_recv.length - ws->_recv.consumed;
    ews_sock_t *sock = ws->_sock;
    int ret;

    if (remaining == 0) {
        if (ws_recv_hdr(ws) < 0) {
            ws->_recv.length = 0;
            return -1;
        }
        ws->_recv.consumed = 0;
        remaining = ws->_recv.length;
    }

    if (flags) {
        if (ws->_recv.state & STATE_TYPE_BINARY) {
            *flags |= EWS_WS_FLAG_BIN;
        } else {
            *flags &= ~EWS_WS_FLAG_BIN;
        }
    }

    ret = sock->ops->recv(sock, buf, remaining);
    if (ret < 0) {
        return -1;
    }

    for (int i = 0; i < ret; i++) {
        buf[i] ^= ws->_recv.key[(i + ws->_recv.consumed) % 4];
    }

    ws->_recv.consumed += ret;
    remaining = ws->_recv.length - ws->_recv.consumed;

    if (remaining == 0 && ws->_recv.opcode & FLAG_FIN) {
        ws->_recv.state &= ~STATE_TYPE_MASK;
        if (flags) {
            *flags |= EWS_WS_FLAG_FIN;
        }
    }

    return ret;
}

static ssize_t ws_ops_send(ews_ws_t *ws, int flags, const char *buf,
        ssize_t buf_sz)
{
    ews_sock_t *sock = ws->_sock;
    uint8_t opcode;

    if (buf_sz < 0) {
        buf_sz = strlen(buf);
    }

    if (ws->_send.state & STATE_TYPE_MASK) {
        opcode = 0x00;
    } else if (flags & EWS_WS_FLAG_BIN) {
        ws->_send.state |= STATE_TYPE_BINARY;
        opcode = 0x02;
    } else {
        ws->_send.state |= STATE_TYPE_TEXT;
        opcode = 0x01;
    }
    if (flags & EWS_WS_FLAG_FIN) {
        ws->_send.state &= ~STATE_TYPE_MASK;
        opcode |= 0x80;
    }

    if (ws_send_hdr(ws, opcode, buf_sz) < 0) {
        return -1;
    }

    if (sock->ops->send(sock, buf, buf_sz) < 0) {
        return -1;
    }

    return buf_sz;
}

static ssize_t ws_vsendf(ews_ws_t *ws, int flags, const char *fmt, va_list va)
{
    va_list va2;
    int len;
    char *buf;

    va_copy(va2, va);
    len = vsnprintf(NULL, 0, fmt, va);
    buf = alloca(len);
    vsprintf(buf, fmt, va2);
    return ws_ops_send(ws, flags, buf, len);
}

static ssize_t ws_ops_sendf(ews_ws_t *ws, int flags, const char *fmt, ...)
{
    va_list va;
    ssize_t ret;

    va_start(va, fmt);
    ret = ws_vsendf(ws, flags, fmt, va);
    va_end(va);
    return ret;
}

const ews_ws_ops_t ws_ops = {
    .recv   = ws_ops_recv,
    .send   = ws_ops_send,
    .sendf  = ws_ops_sendf,
};

static void ws_wrapper(void *arg)
{
    ews_ws_t *ws = (ews_ws_t *) arg;
    ews_sock_t *sock = ws->_sock;
    ews_client_t *client = (ews_client_t *) sock;
    ews_ws_loop_t loop = (ews_ws_loop_t) ws->route->argv[0];
    ews_thread_t *thread = &client->thread;

    loop(ws);
    sock->ops->close(sock);
    free(ws);
    ews_thread_destroy(thread);
}

int ews_ws_upgrade(ews_http_t *http, ews_sock_t *http_sock)
{
    ews_t *ews = http_sock->ews;
    ews_sock_t *ws_sock = NULL;
    ews_thread_t *thread;
    ews_ws_t *ws;

    ews_mutex_lock(&ews->mutex);

#if CONFIG_EWS_WS_CLIENTS > 0
    if (!(http_sock->flags & EWS_SOCK_FLAG_TLS)) {
        ews_client_t *http_client = (ews_client_t *) http_sock;
        ews_client_t *ws_client;
        for (int i = 0; i < countof(ews->ws_client); i++) {
            ws_client = &ews->ws_client[i];
            if (!(ws_client->sock.flags & EWS_SOCK_FLAG_INUSE)) {
                memcpy(ws_client, http_client, sizeof(*ws_client));
                ws_client->sock.flags &= ~EWS_SOCK_FLAG_PROTO_MASK;
                ws_client->sock.flags |= EWS_SOCK_FLAG_PROTO_WEBSOCKET;
                memset(http_client, 0, sizeof(*http_client));
                ws_sock = &ws_client->sock;
                break;
            }
        }
    }
#endif

#if CONFIG_EWS_WSS_CLIENTS > 0
    if (http_sock->flags & EWS_SOCK_FLAG_TLS) {
        ews_client_tls_t *https_client = (ews_client_tls_t *) http_sock;
        ews_client_tls_t *wss_client;
        for (int i = 0; i < countof(ews->wss_client); i++) {
            wss_client = &ews->wss_client[i];
            if (!(wss_client->sock.flags & EWS_SOCK_FLAG_INUSE)) {
                memcpy(wss_client, https_client, sizeof(*wss_client));
                wss_client->sock.flags &= ~EWS_SOCK_FLAG_PROTO_MASK;
                wss_client->sock.flags |= EWS_SOCK_FLAG_PROTO_WEBSOCKET;
                memset(https_client, 0, sizeof(*https_client));
                ws_sock = &wss_client->sock;
                break;
            }
        }
    }
#endif

    ews_mutex_unlock(&ews->mutex);
    if (!ws_sock) {
        return -1;
    }

    ws = calloc(1, sizeof(*ws));
    if (!ws) {
        return -1;
    }
    ws->ops = &ws_ops;
    ws->route = http->route;
    ws->user = http->user;
    ws->_sock = ws_sock;
    ws_sock->user = ws;

    thread = &((ews_client_t *) ws_sock)->thread;

    LOGD("#%d upgrade success", ws_sock->fd);

    return ews_thread_init(thread, ws_wrapper, ws, CONFIG_EWS_WS_STACK_SIZE);
}

typedef struct ws_route_data ws_route_data_t;
struct ws_route_data {
    int flags;
    int version;
    char accept[29];
};

ews_status_t ews_ws_route_handler(ews_http_t *http)
{
    ws_route_data_t *data = (ws_route_data_t *) http->user;
    const char *name, *value;
    int ret;

    switch (http->sess->state) {
    case EWS_STATE_REQ:
        data = calloc(1, sizeof(ws_route_data_t));
        if (!data) {
            return EWS_STATUS_ERROR;
        }
        while (http->ops->get_hdr(http, &name, &value, NULL) > 0) {
            if (strcmp(name, ":method") == 0 && strcmp(value, "GET") == 0) {
                data->flags |= 1 << 0;
            } else if (strcmp(name, "connection") == 0) {
                if (strstr(value, "Upgrade") || strstr(value, "upgrade")) {
                    data->flags |= 1 << 1;
                }
            } else if (strcmp(name, "upgrade") == 0) {
                if (strstr(value, "websocket")) {
                    data->flags |= 1 << 2;
                }
            } else if (strcmp(name, "sec-websocket-key") == 0) {
                ret = ews_ws_gen_accept(data->accept, sizeof(data->accept), value);
                if (ret == 0) {
                    data->flags |= 1 << 3;
                }
            } else if (strcmp(name, "sec-websocket-version") == 0) {
                data->version = strtol(value, NULL, 10);
            }
        }
        if (data->flags == 15) {
            http->user = data;
            return EWS_STATUS_MATCH;
        }
        free(data);
        return EWS_STATUS_NOMATCH;

    case EWS_STATE_RSP:
        if (data->version != 13) {
            http->ops->start_rsp(http, 400, "Bad Request");
            http->ops->send_hdr(http, "Sec-Websocket-Version", "13");
            return EWS_STATUS_DONE;
        }
        http->ops->start_rsp(http, 101, "Switching Protocols");
        http->ops->send_hdr(http, "Upgrade", "websocket");
        http->ops->send_hdr(http, "Connection", "Upgrade");
        http->ops->send_hdr(http, "Sec-WebSocket-Accept", data->accept);
        break;

    case EWS_STATE_FIN:
        free(data);
        http->user = NULL;
        break;
    }

    return EWS_STATUS_DONE;
}
#endif

// SPDX-License-Identifier: MIT
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#include "ews.h"
#include "http.h"
#include "route.h"
#include "server.h"
#include "socket.h"
#include "utils.h"
#include "websocket.h"


static int fill_buf(ews_http_t *http)
{
    ews_sock_t *sock = http->_conn.sock;
    int ret;

    // left-align buffer
    memmove(http->_conn.buf, &http->_conn.buf[http->_conn.bufpos],
            http->_conn.buflen);
    http->_conn.bufpos = 0;

    // recv data into the buffer
    ret = sock->ops->recv(sock, &http->_conn.buf[http->_conn.buflen],
            sizeof(http->_conn.buf) - http->_conn.buflen);
    if (ret <= 0) {
        return ret;
    }
    http->_conn.buflen += ret;
    return 1;
}

static int parse_req(ews_http_t *http)
{
    char *pi = &http->_conn.buf[http->_conn.bufpos];
    char *po = &http->_conn.buf[http->_conn.bufpos];
    char *p, *ver, *name, *value;
    bool have_ver = false;
    int len;

    http->_sess._req.method = po;
    while (isblank(*pi)) {
        pi++;
    }
    while (isalpha(*pi)) {
        *po++ = toupper(*pi++);
    }
    if (!*pi || !isblank(*pi++)) {
        return -1;
    }
    while (isblank(*pi)) {
        pi++;
    }
    if (!*pi || isspace(*pi)) {
        return -1;
    }
    *po++ = '\0';
    http->_sess._req.method_len = strlen(http->_sess._req.method);

    http->_sess._req.path = po;
    while (*pi && !isspace(*pi)) {
        *po++ = *pi++;
    }
    if (!*pi) {
        return -1;
    }
    if (isblank(*pi)) {
        have_ver = true;
    }
    while (isblank(*pi)) {
        pi++;
    }
    *po++ = '\0';
    http->_sess._req.path_len = strlen(http->_sess._req.path);
    if (!have_ver) {
        if (*pi != '\r' || *(pi + 1) != '\n') {
            return -1;
        }
    }

    if (have_ver) {
        ver = po;
        while (*pi && !isspace(*pi)) {
            *po++ = toupper(*pi++);
        }
        while (isblank(*pi)) {
            pi++;
        }
        if (*pi != '\r' || *(pi + 1) != '\n') {
            return -1;
        }
        pi += 2;
        *po++ = '\0';

        if (strcmp(ver, "HTTP/1.1") == 0) {
            http->_sess.flags |= EWS_FLAGS_HTTP11 | EWS_FLAGS_KEEPALIVE;
        }
    }

    http->_sess._req.headers = po;
    while (*pi != '\r' && *(pi + 1) != '\n') {
        name = po;
        if (*pi == ':') {
            return -1;
        }
        while (*pi && !isspace(*pi) && *pi != ':') {
            *po++ = tolower(*pi++);
        }
        if (*pi++ != ':') {
            return -1;
        }
        while(isblank(*pi)) {
            pi++;
        }
        *po++ = '\0';

        value = po;
        while (*pi && *pi != '\r') {
            *po++ = *pi++;
        }
        while (isblank(*(po - 1))) {
            po--;
        }
        if (*pi != '\r' || *(pi + 1) != '\n') {
            return -1;
        }
        pi += 2;
        *po++ = '\0';

        if (strcmp(name, "connection") == 0) {
            if (strstr(value, "close")) {
                http->_sess.flags &= ~EWS_FLAGS_KEEPALIVE;
            } else if (strstr(value, "keep-alive")) {
                http->_sess.flags |= EWS_FLAGS_KEEPALIVE;
            }
        } else if (strcmp(name, "content-length") == 0) {
            http->_sess._req.length = strtol(value, NULL, 10);
        } else if (strcmp(name, "content-type") == 0) {
            if ((p = strstr(value, "multipart/form-data;")) == NULL) {
                goto no_multipart;
            }
            if ((p = strstr(p, "boundary=")) == NULL) {
                goto no_multipart;
            }
            p += 9;
            len = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    p++;
                    len++;
                }
            } else {
                while (*p && *p != ';') {
                    p++;
                    len++;
                }
            }
            http->_sess._req.boundary = malloc(len + 5);
            if (http->_sess._req.boundary == NULL) {
                return -1;
            }
            http->_sess._req.boundary_len = len + 4;
            sprintf((char *) http->_sess._req.boundary, "\r\n--%.*s", len, p);
no_multipart:
        }
    }
    *po++ = '\0';
    *po = '\0';

    return 1;
}

static int parse_mp(ews_http_t *http)
{
    char *pi = &http->_conn.buf[http->_conn.bufpos];
    char *po = &http->_conn.buf[http->_conn.bufpos];

    http->_sess._req.headers = po;
    while (*pi != '\r' && *(pi + 1) != '\n') {
        if (*pi == ':') {
            return -1;
        }
        while (*pi && !isspace(*pi) && *pi != ':') {
            *po++ = tolower(*pi++);
        }
        if (*pi++ != ':') {
            return -1;
        }
        while(isblank(*pi)) {
            pi++;
        }
        *po++ = '\0';

        while (*pi && *pi != '\r') {
            *po++ = *pi++;
        }
        while (isblank(*(po - 1))) {
            po--;
        }
        if (*pi != '\r' || *(pi + 1) != '\n') {
            return -1;
        }
        pi += 2;
        *po++ = '\0';
    }
    *po++ = '\0';
    *po = '\0';

    return 1;
}

static int http_ops_get_hdr(ews_http_t *http, const char **name,
        const char **value, size_t *value_len)
{
    const char *p;

    static const char *names[] = {
        ":method",
        ":path",
        ":scheme",
        ":x-path",
        ":x-query",
    };

    p = http->_sess._req.hdr_name;
    if (p == NULL) {
        p = names[0];
    }
    *name = p;

    if (*name == names[0]) {
        *value = http->_sess._req.method;
        if (value_len) {
            *value_len = http->_sess._req.method_len;
        }
        http->_sess._req.hdr_name = names[1];
        return 1;
    } else if (*name == names[1]) {
        *value = http->_sess._req.path;
        if (value_len) {
            *value_len = http->_sess._req.path_len;
        }
        http->_sess._req.hdr_name = names[2];
        return 1;
    } else if (*name == names[2]) {
        *value = http->_conn.sock->flags & EWS_SOCK_FLAG_TLS ? "https" : "http";
        if (value_len) {
            *value_len = http->_conn.sock->flags & EWS_SOCK_FLAG_TLS ? 5 : 4;
        }
        if (http->_sess._req.x_path) {
            http->_sess._req.hdr_name = names[3];
        } else {
            http->_sess._req.hdr_name = http->_sess._req.headers;
        }
        return 1;
    } else if (*name == names[3]) {
        *value = http->_sess._req.x_path;
        if (value_len) {
            *value_len = http->_sess._req.x_path_len;
        }
        if (http->_sess._req.x_query) {
            http->_sess._req.hdr_name = names[4];
        } else {
            http->_sess._req.hdr_name = http->_sess._req.headers;
        }
        return 1;
    } else if (*name == names[4]) {
        *value = http->_sess._req.x_query;
        if (value_len) {
            *value_len = http->_sess._req.x_query_len;
        }
        http->_sess._req.hdr_name = http->_sess._req.headers;
        return 1;
    } else if (!**name) {
        return -1;
    }

    while (*p++)
        ;
    *value = p;
    if (value_len) {
        *value_len = strlen(p);
    }
    while (*p++)
        ;

    http->_sess._req.hdr_name = p;
    return 1;
}

static ssize_t http_ops_recv(ews_http_t *http, char *buf, ssize_t buf_sz)
{
    ssize_t len, pos;

    if (http->_sess.state != EWS_STATE_REQ_BDY &&
            http->_sess.state != EWS_STATE_REQ_MP_BDY) {
        return -1;
    }

    // if request length specified, limit what we can copy out
    if (http->_sess._req.length >= 0) {
        buf_sz = MIN(buf_sz, http->_sess._req.length -
                http->_sess._req.consumed);
    }

    // limit to buflen
    len = MIN(buf_sz, http->_conn.buflen);
    if (len < 0) {
        len = http->_conn.buflen;
    }

    if (http->_sess._req.boundary) {
        if (http->_sess.state == EWS_STATE_REQ_BDY) {
            pos = ews_findp(&http->_conn.buf[http->_conn.bufpos], len,
                    http->_sess._req.boundary + 2);
            if (pos > 0) {
                len = pos;
            }
            if (pos != 0) {
                goto data;
            }
            // look for initial boundary
            if (http->_conn.buflen >= http->_sess._req.boundary_len) {
                if (memcmp(&http->_conn.buf[http->_conn.bufpos +
                        http->_sess._req.boundary_len - 2], "\r\n", 2) == 0) {
                    http->_sess.state = EWS_STATE_REQ_MP;
                    http->_sess.state_count = -1;
                    http->_conn.bufpos += http->_sess._req.boundary_len;
                    http->_conn.buflen -= http->_sess._req.boundary_len;
                    http->_sess._req.consumed += http->_sess._req.boundary_len;
                    return 0;
                }
                len = http->_sess._req.boundary_len;
                goto data;
            }
            return 0;
        }

        pos = ews_findp(&http->_conn.buf[http->_conn.bufpos], len,
                http->_sess._req.boundary);
        if (pos > 0) {
            len = pos;
        }
        if (pos != 0) {
            goto data;
        }
        // look for terminating boundary
        if (http->_conn.buflen >= http->_sess._req.boundary_len + 4) {
            if (memcmp(&http->_conn.buf[http->_conn.bufpos +
                    http->_sess._req.boundary_len], "--\r\n", 4) == 0) {
                http->_sess.state = EWS_STATE_REQ_BDY;
                http->_sess.state_count = -1;
                http->_conn.bufpos += http->_sess._req.boundary_len + 4;
                http->_conn.buflen -= http->_sess._req.boundary_len + 4;
                http->_sess._req.consumed += http->_sess._req.boundary_len + 4;
                return 0;
            }
        }
        // look for middle boundary
        if (http->_conn.buflen >= http->_sess._req.boundary_len + 2) {
            if (memcmp(&http->_conn.buf[http->_conn.bufpos +
                    http->_sess._req.boundary_len], "\r\n", 2) == 0) {
                http->_sess.state = EWS_STATE_REQ_MP;
                http->_sess.state_count = -1;
                http->_conn.bufpos += http->_sess._req.boundary_len + 2;
                http->_conn.buflen -= http->_sess._req.boundary_len + 2;
                http->_sess._req.consumed += http->_sess._req.boundary_len + 2;
                return 0;
            }
            len = http->_sess._req.boundary_len + 2;
            goto data;
        }
        return 0;
    }

data:

    // copy out what we can from the buffer
    if (buf) {
        memcpy(buf, &http->_conn.buf[http->_conn.bufpos], len);
    }
    http->_conn.bufpos += len;
    http->_conn.buflen -= len;
    http->_sess._req.consumed += len;
    return len;
}

static int http_ops_start_rsp(ews_http_t *http, int code, const char *status)
{
    ews_sock_t *sock = http->_conn.sock;
    ssize_t ret;

    if (http->_sess.flags & EWS_FLAGS_RSP_STARTED) {
        return -1;
    }

    http->_sess._rsp.length = -1;

    ret = sock->ops->sendf(sock, "%s %03d %s\r\n",
            http->_sess.flags & EWS_FLAGS_HTTP11 ? "HTTP/1.1" : "HTTP/1.0",
            code, status);
    if (ret <= 0) {
        return ret;
    }

    http->_sess.flags |= EWS_FLAGS_RSP_STARTED;

    return 1;
}

static int http_ops_send_hdr(ews_http_t *http, const char *name,
        const char *value)
{
    ews_sock_t *sock = http->_conn.sock;
    ssize_t ret;

    if (!(http->_sess.flags & EWS_FLAGS_RSP_STARTED)) {
        return -1;
    }

    if (strcasecmp(name, "Connection") == 0) {
        if (strstr(value, "close")) {
            http->_sess.flags &= ~EWS_FLAGS_KEEPALIVE;
        } else if (strstr(value, "keep-alive")) {
            http->_sess.flags |= EWS_FLAGS_KEEPALIVE;
        }
    } else if (strcasecmp(name, "Content-Length") == 0) {
        http->_sess._rsp.length = strtol(value, NULL, 10);
    } else if (strcasecmp(name, "Transfer-Encoding") == 0) {
        if (strstr(value, "chunked")) {
            http->_sess.flags |= EWS_FLAGS_RSP_CHUNKED;
        }
#if CONFIG_EWS_WS_CLIENTS > 0
    } else if (strcasecmp(name, "Upgrade") == 0) {
        if (strstr(value, "websocket")) {
            http->_sess.flags |= EWS_FLAGS_WEBSOCKET;
        }
#endif
    }

    ret = sock->ops->sendf(sock, "%s: %s\r\n", name, value);
    if (ret <= 0) {
        return ret;
    }
    return 1;
}

static int http_vsendf_hdr(ews_http_t *http, const char *name, const char *fmt,
        va_list va)
{
    va_list va2;
    int value_len;
    char *value;

    va_copy(va2, va);
    value_len = vsnprintf(NULL, 0, fmt, va);
    value = alloca(value_len);
    vsprintf(value, fmt, va2);

    return http_ops_send_hdr(http, name, value);
}

static int http_ops_sendf_hdr(ews_http_t *http, const char *name,
        const char *fmt, ...)
{
    va_list va;
    int ret;

    va_start(va, fmt);
    ret = http_vsendf_hdr(http, name, fmt, va);
    va_end(va);
    return ret;
}

static ssize_t http_ops_send(ews_http_t *http, const char *buf, ssize_t buf_sz)
{
    ews_sock_t *sock = http->_conn.sock;

    if (buf_sz < 0) {
        buf_sz = strlen(buf);
    }

    if (http->_sess._rsp.length >= 0) {
        buf_sz = MIN(buf_sz, http->_sess._rsp.length -
                http->_sess._rsp.produced);
    }

    if (http->_sess.flags & EWS_FLAGS_RSP_CHUNKED) {
        if (sock->ops->sendf(sock, "%x\r\n", buf_sz) < 0) {
            return -1;
        }
    }

    if (sock->ops->send(sock, buf, buf_sz) < 0) {
        return -1;
    }

    if (http->_sess.flags & EWS_FLAGS_RSP_CHUNKED) {
        if (sock->ops->send(sock, "\r\n", 2) < 0) {
            return -1;
        }
    }

    http->_sess._rsp.produced += buf_sz;
    if (http->_sess._rsp.produced == http->_sess._rsp.length) {
        http->_sess.state = EWS_STATE_FIN;
        http->_sess.state_count = -1;
    }

    return buf_sz;
}

static ssize_t http_vsendf(ews_http_t *http, const char *fmt, va_list va)
{
    va_list va2;
    int len;
    char *buf;

    va_copy(va2, va);
    len = vsnprintf(NULL, 0, fmt, va);
    buf = alloca(len);
    vsprintf(buf, fmt, va2);
    return http_ops_send(http, buf, len);
}

static ssize_t http_ops_sendf(ews_http_t *http, const char *fmt, ...)
{
    va_list va;
    ssize_t ret;

    va_start(va, fmt);
    ret = http_vsendf(http, fmt, va);
    va_end(va);
    return ret;
}

static const ews_http_ops_t http_ops = {
    .get_hdr    = http_ops_get_hdr,
    .recv       = http_ops_recv,
    .start_rsp  = http_ops_start_rsp,
    .send_hdr   = http_ops_send_hdr,
    .sendf_hdr  = http_ops_sendf_hdr,
    .send       = http_ops_send,
    .sendf      = http_ops_sendf,
};

static int start_req(ews_http_t *http)
{
    int pos, ret;

    http->_sess._req.length = -1;

    // check if request is too long
    if (http->_conn.buflen > sizeof(http->_conn.buf) - 4) {
        return -2;
    }

    // look for termination
    pos = ews_find(http->_conn.buf, http->_conn.buflen, "\r\n\r\n");
    // not found? try again later
    if (pos < 0) {
        return 0;
    }

    ret = parse_req(http);
    if (ret < 0) {
        return ret;
    }

    http->_conn.bufpos += pos + 4;
    http->_conn.buflen -= pos + 4;

    if (http->_sess._req.path[0] == '/') {
        http->_sess._req.x_path = strdup(http->_sess._req.path);
        if (!http->_sess._req.x_path) {
            return -1;
        }
        ews_parse_uri(http->_sess._req.path, (char *) http->_sess._req.x_path,
                &http->_sess._req.x_path_len, &http->_sess._req.x_query,
                &http->_sess._req.x_query_len);
    }

    LOGI("#%d %s %s", http->_conn.sock->fd, http->_sess._req.method,
            http->_sess._req.x_path ? http->_sess._req.x_path :
            http->_sess._req.path);

    return 1;
}

static int fetch_mp(ews_http_t *http)
{
    int pos, ret;

    // check if headers are too long
    if (http->_conn.buflen > sizeof(http->_conn.buf) - 4) {
        return -2;
    }

    // look for termination
    pos = ews_find(&http->_conn.buf[http->_conn.bufpos], http->_conn.buflen,
            "\r\n\r\n");
    // not found? try again later
    if (pos < 0) {
        return 0;
    }

    ret = parse_mp(http);
    if (ret < 0) {
        return ret;
    }

    http->_conn.bufpos += pos + 4;
    http->_conn.buflen -= pos + 4;
    http->_sess._req.consumed += pos + 4;
    http->_sess._req.hdr_name = http->_sess._req.headers;
    return 1;
}

static ews_status_t find_route(ews_http_t *http)
{
    ews_t *ews = http->_conn.sock->ews;
    ews_status_t status;
    const char *path = http->_sess._req.x_path ? http->_sess._req.x_path :
            http->_sess._req.path;

    http->route = ews->route_first;

    while (http->route) {
        if (!ews_fnmatch(http->route->pattern, path)) {
            http->route = http->route->next;
            continue;
        }

        http->_sess._req.hdr_name = NULL;
        status = http->route->handler(http);
        if (status == EWS_STATUS_MATCH) {
            LOGD("#%d found!", http->_conn.sock->fd);
            break;
        }
        if (status != EWS_STATUS_NOMATCH) {
            status = EWS_STATUS_ERROR;
            break;
        }
        http->route = http->route->next;
    }

    if (status != EWS_STATUS_ERROR && status != EWS_STATUS_MATCH) {
        http->route = &ews_route_404;
        http->_sess._req.hdr_name = NULL;
        status = http->route->handler(http);
        LOGD("#%d 404", http->_conn.sock->fd);
    }

    if (status == EWS_STATUS_MATCH) {
        if (http->_sess._req.length >= 0 || http->_sess._req.boundary) {
            http->_sess.state = EWS_STATE_REQ_BDY;
        } else {
            http->_sess.state = EWS_STATE_RSP;
        }
        http->_sess.state_count = -1;
    }

    return status;
}

static void finalize(ews_http_t *http)
{
    ews_sock_t *sock = http->_conn.sock;
    ews_flags_t flags = http->_sess.flags;

    if (!http->route) {
        return;
    }

    if (!(flags & EWS_FLAGS_WEBSOCKET)) {
        if (http->_sess.state == EWS_STATE_FIN) {
            // send final chunk if chunked encoding
            http->ops->send(http, "", 0);
        }

        if (!(flags & EWS_FLAGS_KEEPALIVE) || !(flags & EWS_FLAGS_RSP_CHUNKED ||
                http->_sess._rsp.length >= 0)) {
            // shutdown the connection
            sock->ops->shutdown(sock);
        }
    }

    // free and zero session data
    if (http->_sess._req.x_path) {
        free((void *) http->_sess._req.x_path);
    }
    if (http->_sess._req.boundary) {
        free((void *) http->_sess._req.boundary);
    }

    // zero session data
    memset(&http->_sess, 0, sizeof(http->_sess));

#if CONFIG_EWS_WS_CLIENTS > 0 || CONFIG_EWS_WSS_CLIENTS > 0
    if (flags & EWS_FLAGS_WEBSOCKET) {
        ews_ws_upgrade(http, sock);

        // free session data
        free(http);
    }
#endif
}

static void handle_error(ews_http_t *http, int code)
{
    ews_sock_t *sock = http->_conn.sock;

    http->_sess.flags &= ~EWS_FLAGS_KEEPALIVE;
    if (http->_sess.state < EWS_STATE_RSP) {
        if (code == 431) {
            http->ops->start_rsp(http, code, "Request Header Fields Too Large");
            http->ops->send_hdr(http, "Content-Length", "40");
            sock->ops->send(sock,
                    "\r\n<h1>Request Header Fields Too Large</h1>", 42);
        } else {
            http->ops->start_rsp(http, code, "Internal Server Error");
            http->ops->send_hdr(http, "Content-Length", "30");
            sock->ops->send(sock, "\r\n<h1>Internal Server Error</h1>", 32);
        }
    }

    if (http->_sess.state != EWS_STATE_REQ &&
            http->_sess.state != EWS_STATE_FIN) {
        http->_sess.state = EWS_STATE_FIN;
        http->_sess.state_count = 0;
        http->route->handler(http);
    }

    finalize(http);
}

static void handle_status(ews_http_t *http, ews_status_t status)
{
    ews_sock_t *sock = http->_conn.sock;

    http->_sess.state_count++;
    if (http->_sess.state_count == 0) {
        return;
    }

again:

    switch (status) {
    case EWS_STATUS_ERROR:
        handle_error(http, 500);
        break;

    case EWS_STATUS_CLOSE:
        if (http->_sess.state != EWS_STATE_REQ &&
                http->_sess.state != EWS_STATE_FIN) {
            http->_sess.state = EWS_STATE_FIN;
            http->_sess.state_count = 0;
            http->route->handler(http);
        }
        finalize(http);
        break;

    case EWS_STATUS_MATCH:
    case EWS_STATUS_NOMATCH:
        if (http->_sess.state != EWS_STATE_REQ) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        break;

    case EWS_STATUS_MORE:
        if (http->_sess.state == EWS_STATE_FIN) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        break;

    case EWS_STATUS_SKIP:
        switch (http->_sess.state) {
        case EWS_STATE_REQ_BDY:
            http->ops->recv(http, NULL, -1);
            if (http->_sess._req.consumed == http->_sess._req.length) {
                http->_sess.state = EWS_STATE_RSP;
                http->_sess.state_count = 0;
            }
            break;

        case EWS_STATE_REQ_MP:
            http->_sess.state = EWS_STATE_REQ_MP_BDY;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_REQ_MP_BDY:
            http->ops->recv(http, NULL, -1);
            break;

        case EWS_STATE_RSP:
            if (!(http->_sess.flags & EWS_FLAGS_RSP_STARTED)) {
                status = EWS_STATUS_ERROR;
                goto again;
            }
            sock->ops->send(sock, "\r\n", 2);
            http->_sess.state = EWS_STATE_RSP_BDY;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_RSP_BDY:
            http->_sess.state = EWS_STATE_FIN;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_FIN:
            finalize(http);
            break;

        default:
            status = EWS_STATUS_ERROR;
            goto again;
        }

        if (http->_sess.state_count == -1) {
            http->_sess.state_count = 0;
            goto again;
        }
        break;

    case EWS_STATUS_NEXT:
        switch (http->_sess.state) {
        case EWS_STATE_REQ_BDY:
            if (http->_sess._req.consumed == http->_sess._req.length) {
                http->_sess.state = EWS_STATE_RSP;
                http->_sess.state_count = 0;
            }
            break;

        case EWS_STATE_REQ_MP:
            http->_sess.state = EWS_STATE_REQ_MP_BDY;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_RSP:
            if (!(http->_sess.flags & EWS_FLAGS_RSP_STARTED)) {
                status = EWS_STATUS_ERROR;
                goto again;
            }
            sock->ops->send(sock, "\r\n", 2);
            http->_sess.state = EWS_STATE_RSP_BDY;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_RSP_BDY:
            http->_sess.state = EWS_STATE_FIN;
            http->_sess.state_count = 0;
            break;

        case EWS_STATE_FIN:
            finalize(http);
            break;

        default:
            status = EWS_STATUS_ERROR;
            goto again;
        }
        break;

    case EWS_STATUS_DONE:
        if (!(http->_sess.flags & EWS_FLAGS_RSP_STARTED)) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        if (http->_sess.state == EWS_STATE_RSP) {
            sock->ops->send(sock, "\r\n", 2);
        }
        if (http->_sess.state != EWS_STATE_FIN) {
            http->_sess.state = EWS_STATE_FIN;
            http->route->handler(http);
        }
        finalize(http);
        break;

    default:
       status = EWS_STATUS_ERROR;
       goto again;
    }
}

static void on_connect(ews_sock_t *sock)
{
    ews_http_t *http = calloc(1, sizeof(*http));
    sock->user = http;
    http->_conn.sock = sock;
    http->sess = &http->_sess;
    http->ops = &http_ops;

    sock->flags |= EWS_SOCK_FLAG_PROTO_HTTP | EWS_SOCK_FLAG_CONNECTED;
}

static void on_close(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;

    finalize(http);
    free(sock->user);
    sock->user = NULL;
    sock->ops->close(sock);
}

static bool want_read(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;

    return !(http->_sess.state & EWS_STATE_RSP);
}

static bool want_write(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;

    return !!(http->_sess.state & EWS_STATE_RSP);
}

static void do_read(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;
    ews_status_t status;
    size_t len;
    int ret;

    ret = fill_buf(http);
    if (ret < 0) {
        handle_error(http, 500);
    }
    if (ret <= 0) {
        return;
    }

    while (http->_sess.state < EWS_STATE_RSP) {
        len = http->_conn.buflen;

        switch (http->_sess.state) {
        case EWS_STATE_REQ:
            ret = start_req(http);
            if (ret < 0) {
                handle_error(http, ret == -2 ? 431 : 500);
            }
            if (ret <= 0) {
                return;
            }
            status = find_route(http);
            handle_status(http, status);
            if (status <= EWS_STATUS_CLOSE) {
                return;
            }
            break;

        case EWS_STATE_REQ_MP:
            ret = fetch_mp(http);
            if (ret < 0) {
                handle_error(http, ret == -2 ? 431 : 500);
            }
            if (ret <= 0) {
                return;
            }
            status = http->route->handler(http);
            handle_status(http, status);
            if (status <= EWS_STATUS_CLOSE) {
                return;
            }
            break;

        case EWS_STATE_REQ_BDY:
        case EWS_STATE_REQ_MP_BDY:
            status = http->route->handler(http);
            handle_status(http, status);
            break;
        }

        if (len == http->_conn.buflen) {
            break;
        }
    }
}

static void do_write(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;
    ews_status_t status;

    status = http->route->handler(http);
    handle_status(http, status);
}

const ews_sock_evt_t http_sock_evt = {
    .on_connect = on_connect,
    .on_close   = on_close,
    .want_read  = want_read,
    .want_write = want_write,
    .do_read    = do_read,
    .do_write   = do_write,
};

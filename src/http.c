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


static int fill_buf(ews_http_t *http)
{
    ews_sock_t *sock = http->conn.sock;
    int ret;

    // left-align buffer
    memmove(http->conn.buf, &http->conn.buf[http->conn.bufpos],
            http->conn.buflen);
    http->conn.bufpos = 0;

    // recv data into the buffer
    ret = sock->ops->recv(sock, &http->conn.buf[http->conn.buflen],
            sizeof(http->conn.buf) - http->conn.buflen);
    if (ret <= 0) {
        return ret;
    }
    http->conn.buflen += ret;
    return 1;
}

static int parse_req(ews_http_t *http)
{
    char *pi = &http->conn.buf[http->conn.bufpos];
    char *po = &http->conn.buf[http->conn.bufpos];
    char *p, *ver, *name, *value;
    bool have_ver = false;
    int len;

    http->sess.req.method = po;
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

    http->sess.req.path = po;
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
            http->sess.flags |= EWS_FLAGS_HTTP11 | EWS_FLAGS_KEEPALIVE;
        }
    }

    http->sess.req.headers = po;
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
                http->sess.flags &= ~EWS_FLAGS_KEEPALIVE;
            } else if (strstr(value, "keep-alive")) {
                http->sess.flags |= EWS_FLAGS_KEEPALIVE;
            }
        } else if (strcmp(name, "content-length") == 0) {
            http->sess.req.length = strtol(value, NULL, 10);
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
            http->sess.req.boundary = malloc(len + 5);
            if (http->sess.req.boundary == NULL) {
                return -1;
            }
            http->sess.req.boundary_len = len + 4;
            sprintf((char *) http->sess.req.boundary, "\r\n--%.*s", len, p);
no_multipart:
        }
    }
    *po++ = '\0';
    *po = '\0';

    return 1;
}

static int parse_mp(ews_http_t *http)
{
    char *pi = &http->conn.buf[http->conn.bufpos];
    char *po = &http->conn.buf[http->conn.bufpos];

    http->sess.req.headers = po;
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
        const char **value)
{
    const char *p;

    static const char *names[] = {
        ":method",
        ":path",
        ":scheme",
        ":x-path",
        ":x-query",
    };

    p = http->sess.req.hdr_name;
    if (p == NULL) {
        p = names[0];
    }
    *name = p;

    if (*name == names[0]) {
        *value = http->sess.req.method;
        http->sess.req.hdr_name = names[1];
        return 1;
    } else if (*name == names[1]) {
        *value = http->sess.req.path;
        http->sess.req.hdr_name = names[2];
        return 1;
    } else if (*name == names[2]) {
        *value = http->conn.sock->flags & EWS_SOCK_FLAG_TLS ? "https" : "http";
        http->sess.req.hdr_name = names[3];
        return 1;
    } else if (*name == names[3]) {
        *value = http->sess.req.x_path;
        http->sess.req.hdr_name = names[4];
        return 1;
    } else if (*name == names[4]) {
        *value = http->sess.req.x_query;
        http->sess.req.hdr_name = http->sess.req.headers;
        return 1;
    } else if (!**name) {
        return -1;
    }

    while (*p++)
        ;
    *value = p;
    while (*p++)
        ;

    http->sess.req.hdr_name = p;
    return 1;
}

static int http_ops_recv(ews_http_t *http, char *buf, int buf_sz)
{
    int len, pos;

    if (http->sess.state != EWS_STATE_REQ_BDY &&
            http->sess.state != EWS_STATE_REQ_MP_BDY) {
        return -1;
    }

    // if request length specified, limit what we can copy out
    if (http->sess.req.length >= 0) {
        buf_sz = MIN(buf_sz, http->sess.req.length - http->sess.req.consumed);
    }

    // limit to buflen
    len = MIN(buf_sz, http->conn.buflen);
    if (len < 0) {
        len = http->conn.buflen;
    }

    if (http->sess.req.boundary) {
        if (http->sess.state == EWS_STATE_REQ_BDY) {
            pos = findp(&http->conn.buf[http->conn.bufpos], len,
                    http->sess.req.boundary + 2);
            if (pos > 0) {
                len = pos;
            }
            if (pos != 0) {
                goto data;
            }
            // look for initial boundary
            if (http->conn.buflen >= http->sess.req.boundary_len) {
                if (memcmp(&http->conn.buf[http->conn.bufpos +
                        http->sess.req.boundary_len - 2], "\r\n", 2) == 0) {
                    http->sess.state = EWS_STATE_REQ_MP;
                    http->sess.state_count = -1;
                    http->conn.bufpos += http->sess.req.boundary_len;
                    http->conn.buflen -= http->sess.req.boundary_len;
                    http->sess.req.consumed += http->sess.req.boundary_len;
                    return 0;
                }
                len = http->sess.req.boundary_len;
                goto data;
            }
            return 0;
        }

        pos = findp(&http->conn.buf[http->conn.bufpos], len,
                http->sess.req.boundary);
        if (pos > 0) {
            len = pos;
        }
        if (pos != 0) {
            goto data;
        }
        // look for terminating boundary
        if (http->conn.buflen >= http->sess.req.boundary_len + 4) {
            if (memcmp(&http->conn.buf[http->conn.bufpos +
                    http->sess.req.boundary_len], "--\r\n", 4) == 0) {
                http->sess.state = EWS_STATE_REQ_BDY;
                http->sess.state_count = -1;
                http->conn.bufpos += http->sess.req.boundary_len + 4;
                http->conn.buflen -= http->sess.req.boundary_len + 4;
                http->sess.req.consumed += http->sess.req.boundary_len + 4;
                return 0;
            }
        }
        // look for middle boundary
        if (http->conn.buflen >= http->sess.req.boundary_len + 2) {
            if (memcmp(&http->conn.buf[http->conn.bufpos +
                    http->sess.req.boundary_len], "\r\n", 2) == 0) {
                http->sess.state = EWS_STATE_REQ_MP;
                http->sess.state_count = -1;
                http->conn.bufpos += http->sess.req.boundary_len + 2;
                http->conn.buflen -= http->sess.req.boundary_len + 2;
                http->sess.req.consumed += http->sess.req.boundary_len + 2;
                return 0;
            }
            len = http->sess.req.boundary_len + 2;
            goto data;
        }
        return 0;
    }

data:

    // copy out what we can from the buffer
    if (buf) {
        memcpy(buf, &http->conn.buf[http->conn.bufpos], len);
    }
    http->conn.bufpos += len;
    http->conn.buflen -= len;
    http->sess.req.consumed += len;
    return len;
}

static int http_ops_start_rsp(ews_http_t *http, int code, const char *status)
{
    ews_sock_t *sock = http->conn.sock;
    int ret;

    if (http->sess.flags & EWS_FLAGS_RSP_STARTED) {
        return -1;
    }

    http->sess.rsp.length = -1;

    ret = sock->ops->sendf(sock, "%s %03d %s\r\n",
            http->sess.flags & EWS_FLAGS_HTTP11 ? "HTTP/1.1" : "HTTP/1.0",
            code, status);
    if (ret <= 0) {
        return ret;
    }

    http->sess.flags |= EWS_FLAGS_RSP_STARTED;

    return ret;
}

static int http_ops_send_hdr(ews_http_t *http, const char *name,
        const char *value)
{
    ews_sock_t *sock = http->conn.sock;

    if (!(http->sess.flags & EWS_FLAGS_RSP_STARTED)) {
        return -1;
    }

    if (strcasecmp(name, "Connection") == 0) {
        if (strstr(value, "close")) {
            http->sess.flags &= ~EWS_FLAGS_KEEPALIVE;
        } else if (strstr(value, "keep-alive")) {
            http->sess.flags |= EWS_FLAGS_KEEPALIVE;
        }
    } else if (strcasecmp(name, "Content-Length") == 0) {
        http->sess.rsp.length = strtol(value, NULL, 10);
    } else if (strcasecmp(name, "Transfer-Encoding") == 0) {
        if (strstr(value, "chunked")) {
            http->sess.flags |= EWS_FLAGS_RSP_CHUNKED;
        }
    }

    return sock->ops->sendf(sock, "%s: %s\r\n", name, value);
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

static int http_ops_send(ews_http_t *http, const char *buf, int buf_sz)
{
    ews_sock_t *sock = http->conn.sock;

    if (buf_sz < 0) {
        buf_sz = strlen(buf);
    }

    if (http->sess.rsp.length >= 0) {
        buf_sz = MIN(buf_sz, http->sess.rsp.length - http->sess.rsp.produced);
    }

    if (http->sess.flags & EWS_FLAGS_RSP_CHUNKED) {
        if (sock->ops->sendf(sock, "%x\r\n", buf_sz) < 0) {
            return -1;
        }
    }

    if (sock->ops->send(sock, buf, buf_sz) < 0) {
        return -1;
    }

    if (http->sess.flags & EWS_FLAGS_RSP_CHUNKED) {
        if (sock->ops->send(sock, "\r\n", 2) < 0) {
            return -1;
        }
    }

    http->sess.rsp.produced += buf_sz;
    if (http->sess.rsp.produced == http->sess.rsp.length) {
        http->sess.state = EWS_STATE_FIN;
        http->sess.state_count = -1;
    }

    return buf_sz;
}

static int http_vsendf(ews_http_t *http, const char *fmt, va_list va)
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

static int http_ops_sendf(ews_http_t *http, const char *fmt, ...)
{
    va_list va;
    int ret;

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
    int pos, ret, len;

    http->sess.req.length = -1;

    // check if request is too long
    if (http->conn.buflen > sizeof(http->conn.buf) - 4) {
        return -2;
    }

    // look for termination
    pos = find(http->conn.buf, http->conn.buflen, "\r\n\r\n");
    // not found? try again later
    if (pos < 0) {
        return 0;
    }

    ret = parse_req(http);
    if (ret < 0) {
        return ret;
    }

    http->conn.bufpos += pos + 4;
    http->conn.buflen -= pos + 4;
    http->sess.req.hdr_name = http->sess.req.method;

    len = strlen(http->sess.req.path);
    http->sess.req.x_path = malloc(len + 1);
    if (!http->sess.req.x_path) {
        return -1;
    }

    parse_uri(http->sess.req.path, (char *) http->sess.req.x_path,
            &http->sess.req.x_query);

    LOGI("#%d %s %s", http->conn.sock->fd, http->sess.req.method,
            http->sess.req.x_path);

    return 1;
}

static int fetch_mp(ews_http_t *http)
{
    int pos, ret;

    // check if headers are too long
    if (http->conn.buflen > sizeof(http->conn.buf) - 4) {
        return -2;
    }

    // look for termination
    pos = find(&http->conn.buf[http->conn.bufpos], http->conn.buflen,
            "\r\n\r\n");
    // not found? try again later
    if (pos < 0) {
        return 0;
    }

    ret = parse_mp(http);
    if (ret < 0) {
        return ret;
    }

    http->conn.bufpos += pos + 4;
    http->conn.buflen -= pos + 4;
    http->sess.req.consumed += pos + 4;
    http->sess.req.hdr_name = http->sess.req.headers;
    return 1;
}

static ews_status_t find_route(ews_http_t *http)
{
    ews_t *ews = http->conn.sock->ews;
    const ews_route_t *route = ews->route_first;
    ews_status_t status;

    while (route) {
        if (!fnmatch(route->pattern, http->sess.req.x_path)) {
            route = route->next;
            continue;
        }

        http->sess.req.hdr_name = NULL;

        status = route->handler(http, &http->sess);
        if (status == EWS_STATUS_MATCH) {
            LOGD("#%d found!", http->conn.sock->fd);
            break;
        }
        if (status != EWS_STATUS_NOMATCH) {
            status = EWS_STATUS_ERROR;
            break;
        }
        route = route->next;
    }

    if (route == NULL) {
        route = &ews_route_404;
        status = route->handler(http, &http->sess);
        LOGD("#%d 404", http->conn.sock->fd);
    }

    http->sess.route = route;

    if (status == EWS_STATUS_MATCH) {
        if (http->sess.req.length >= 0 || http->sess.req.boundary) {
            http->sess.state = EWS_STATE_REQ_BDY;
        } else {
            http->sess.state = EWS_STATE_RSP;
        }
        http->sess.state_count = -1;
    }

    return status;
}

static void finalize(ews_http_t *http)
{
    ews_sock_t *sock = http->conn.sock;

    if (!http->sess.route) {
        return;
    }

    if (http->sess.state == EWS_STATE_FIN) {
        // send final chunk if chunked encoding
        http->ops->send(http, "", 0);
    }

    if (!(http->sess.flags & EWS_FLAGS_KEEPALIVE)) {
        // shutdown the connection
        sock->ops->shutdown(sock);
    }

    // free session data
    if (http->sess.req.x_path) {
        free((void *) http->sess.req.x_path);
    }
    if (http->sess.req.boundary) {
        free((void *) http->sess.req.boundary);
    }

    // zero session data
    memset(&http->sess, 0, sizeof(http->sess));
}

static void handle_error(ews_http_t *http, int code)
{
    ews_sock_t *sock = http->conn.sock;

    http->sess.flags &= ~EWS_FLAGS_KEEPALIVE;
    if (http->sess.state < EWS_STATE_RSP) {
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

    if (http->sess.route && http->sess.state != EWS_STATE_FIN) {
        http->sess.state = EWS_STATE_FIN;
        http->sess.state_count = 0;
        http->sess.route->handler(http, &http->sess);
    }

    finalize(http);
}

static void handle_status(ews_http_t *http, ews_status_t status)
{
    ews_sock_t *sock = http->conn.sock;

    http->sess.state_count++;
    if (http->sess.state_count == 0) {
        return;
    }

again:

    switch (status) {
    case EWS_STATUS_ERROR:
        handle_error(http, 500);
        break;

    case EWS_STATUS_CLOSE:
        if (http->sess.route && http->sess.state != EWS_STATE_FIN) {
            http->sess.state = EWS_STATE_FIN;
            http->sess.state_count = 0;
            http->sess.route->handler(http, &http->sess);
        }
        finalize(http);
        break;

    case EWS_STATUS_MATCH:
    case EWS_STATUS_NOMATCH:
        if (http->sess.state != EWS_STATE_REQ) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        break;

    case EWS_STATUS_MORE:
        if (http->sess.state == EWS_STATE_FIN) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        break;

    case EWS_STATUS_SKIP:
        switch (http->sess.state) {
        case EWS_STATE_REQ_BDY:
            http->ops->recv(http, NULL, -1);
            if (http->sess.req.consumed == http->sess.req.length) {
                http->sess.state = EWS_STATE_RSP;
                http->sess.state_count = 0;
            }
            break;

        case EWS_STATE_REQ_MP:
            http->sess.state = EWS_STATE_REQ_MP_BDY;
            http->sess.state_count = 0;
            break;

        case EWS_STATE_REQ_MP_BDY:
            http->ops->recv(http, NULL, -1);
            break;

        case EWS_STATE_FIN:
            finalize(http);
            break;

        default:
            status = EWS_STATUS_ERROR;
            goto again;
        }

        if (http->sess.state_count == -1) {
            http->sess.state_count = 0;
            goto again;
        }
        break;

    case EWS_STATUS_NEXT:
        switch (http->sess.state) {
        case EWS_STATE_REQ_BDY:
            if (http->sess.req.consumed == http->sess.req.length) {
                http->sess.state = EWS_STATE_RSP;
                http->sess.state_count = 0;
            }
            break;

        case EWS_STATE_REQ_MP:
            http->sess.state = EWS_STATE_REQ_MP_BDY;
            http->sess.state_count = 0;
            break;

        case EWS_STATE_RSP:
            sock->ops->send(sock, "\r\n", 2);
            http->sess.state = EWS_STATE_RSP_BDY;
            http->sess.state_count = 0;
            break;

        case EWS_STATE_RSP_BDY:
            http->sess.state = EWS_STATE_FIN;
            http->sess.state_count = 0;
            break;

        case EWS_STATE_FIN:
            finalize(http);
            break;

        default:
            break;
        }
        break;

    case EWS_STATUS_DONE:
        if (http->sess.state < EWS_STATE_RSP) {
            status = EWS_STATUS_ERROR;
            goto again;
        }
        if (http->sess.state < EWS_STATE_RSP) {
            http->sess.flags &= ~EWS_FLAGS_KEEPALIVE;
        }
        if (http->sess.state != EWS_STATE_FIN) {
            http->sess.state = EWS_STATE_FIN;
            http->sess.route->handler(http, &http->sess);
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
    http->conn.sock = sock;
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

    return !(http->sess.state & EWS_STATE_RSP);
}

static bool want_write(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;

    return !!(http->sess.state & EWS_STATE_RSP);
}

static void do_read(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;
    ews_status_t status;
    int ret, len;

    ret = fill_buf(http);
    if (ret < 0) {
        handle_error(http, 500);
    }
    if (ret <= 0) {
        return;
    }

    while (http->sess.state < EWS_STATE_RSP) {
        len = http->conn.buflen;

        switch (http->sess.state) {
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
            status = http->sess.route->handler(http, &http->sess);
            handle_status(http, status);
            if (status <= EWS_STATUS_CLOSE) {
                return;
            }
            break;

        case EWS_STATE_REQ_BDY:
        case EWS_STATE_REQ_MP_BDY:
            status = http->sess.route->handler(http, &http->sess);
            handle_status(http, status);
            break;
        }

        if (len == http->conn.buflen) {
            break;
        }
    }
}

static void do_write(ews_sock_t *sock)
{
    ews_http_t *http = sock->user;
    ews_status_t status;

    status = http->sess.route->handler(http, &http->sess);
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

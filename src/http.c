// SPDX-License-Identifier: MIT
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "http.h"
#include "ews.h"
#include "route.h"
#include "server.h"
#include "socket.h"
#include "utils.h"


static void finalize(ews_sess_t *sess);
static void http_error(ews_sess_t *sess, int code, const char *msg);

static ssize_t http_recv(ews_sess_t *sess, void *buf, size_t len)
{
    // ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    return -1;
}

static ssize_t http_send(ews_sess_t *sess, const void *buf, size_t len)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ews_sock_t *sock = sess->sock;
    ssize_t ret = 0;
    size_t total = 0;

    if (data->block.state != EWS_SESS_RESPONSE_BODY) {
        LOGD("attempted to send data in non-response-data state");
        http_error(sess, 500, "Internal Server Error");
        return -1;
    }

    if (data->block.flags & EWS_HTTP_FLAGS_RESPONSE_CHUNKED) {
        char s[11];
        ret = snprintf(s, sizeof(s), "%lX\r\n", len);
        ret = sock->ops->send(sock, s, ret);
        if (ret < 0) {
            finalize(sess);
            return -1;
        }
        total += ret;
    } else if (data->block.response.length > 0) {
        len = MIN(len, data->block.response.length);
    }

    ret = sock->ops->send(sock, buf, len);
    if (ret < 0) {
        finalize(sess);
        return -1;
    }
    total += ret;

    if (data->block.flags & EWS_HTTP_FLAGS_RESPONSE_CHUNKED) {
        ret = sock->ops->send(sock, "\r\n", 2);
        if (ret < 0) {
            finalize(sess);
            return -1;
        }
        total += ret;
    } else if (data->block.response.length > 0) {
        data->block.response.length -= ret;
    }

    return total;
}

static void http_vsendf(ews_sess_t *sess, const char *fmt, va_list va)
{
    va_list va2;
    size_t len;
    char *buf;

    va_copy(va2, va);
    len = vsnprintf(NULL, 0, fmt, va);
    buf = alloca(len);
    vsprintf(buf, fmt, va2);
    http_send(sess, buf, len);
}

static void http_sendf(ews_sess_t *sess, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    http_vsendf(sess, fmt, va);
    va_end(va);
}

static ssize_t http_raw_send(ews_sess_t *sess, const void *buf, size_t len)
{
    ews_sock_t *sock = sess->sock;
    ssize_t ret;

    ret = sock->ops->send(sock, buf, len);
    if (ret < 0) {
        finalize(sess);
        return -1;
    }
    return ret;
}

static void http_raw_vsendf(ews_sess_t *sess, const char *fmt, va_list va)
{
    va_list va2;
    size_t len;
    char *buf;

    va_copy(va2, va);
    len = vsnprintf(NULL, 0, fmt, va);
    buf = alloca(len);
    vsprintf(buf, fmt, va2);
    http_raw_send(sess, buf, len);
}

static void http_raw_sendf(ews_sess_t *sess, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    http_raw_vsendf(sess, fmt, va);
    va_end(va);
}

static void http_status(ews_sess_t *sess, int code, const char *msg)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    if (data->block.version == EWS_HTTP_VERSION_09) {
        return;
    }

    if (data->block.state > EWS_SESS_RESPONSE_BEGIN) {
        finalize(sess);
        data->block.flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return;
    }

    http_raw_sendf(sess, "%s %d %s\r\n",
            data->block.version == EWS_HTTP_VERSION_11 ? "HTTP/1.1" :
            "HTTP/1.0", code, msg);
}

static void http_error(ews_sess_t *sess, int code, const char *msg)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    const char *fmt1, *fmt2;
    size_t len;

    if (data->block.state <= EWS_SESS_RESPONSE_BEGIN &&
            data->block.version > EWS_HTTP_VERSION_09) {

        fmt1 = "Content-Type: text/html\r\nContent-Length: %lu\r\n\r\n";
        fmt2 = "<h1>%s</h1>";
        len = snprintf(NULL, 0, fmt2, msg);

        http_status(sess, code, msg);
        http_raw_sendf(sess, fmt1, len);
        http_raw_sendf(sess, fmt2, msg);
    }

    finalize(sess);
}

static void http_header(ews_sess_t *sess, const char *name, const char *value)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    if (data->block.state != EWS_SESS_RESPONSE_HEADER) {
        LOGD("attempted to send headers in non-response header state");
        http_error(sess, 500, "Internal Server Error");
        return;
    }

    if (strcasecmp(name, "Connection") == 0) {
        if (strstr(value, "close")) {
            data->block.flags &= ~EWS_HTTP_FLAGS_KEEPALIVE;
        } else if (strstr(value, "keep-alive")) {
            data->block.flags |= EWS_HTTP_FLAGS_KEEPALIVE;
        }
    } else if (strcasecmp(name, "Content-Length") == 0) {
        data->block.response.length = strtol(value, NULL, 10);
    } else if (strcasecmp(name, "Transfer-Encoding") == 0) {
        if (strstr(value, "chunked")) {
            data->block.flags |= EWS_HTTP_FLAGS_RESPONSE_CHUNKED;
        }
    }

    http_raw_sendf(sess, "%s: %s\r\n", name, value);
}

static const ews_sess_ops_t http_sess_ops = {
    .recv = http_recv,
    .send = http_send,
    .sendf = http_sendf,
    .status = http_status,
    .error = http_error,
    .header = http_header,
};

static ews_route_status_t call_handler(ews_sess_t *sess)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ews_sock_t *sock = sess->sock;
    ews_route_status_t status;

    if (data->block.state != data->block.prev_state) {
        data->block.state_count = 0;
    } else {
        data->block.state_count++;
    }
    status = data->block.route->handler(sess, data->block.state);
    data->block.prev_state = data->block.state;

    switch (status) {
    default:
    case EWS_ROUTE_STATUS_ERROR:
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        if (data->block.state > EWS_SESS_REQUEST_BEGIN) {
            http_error(sess, 500, "Internal Server Error");
            return EWS_ROUTE_STATUS_ERROR;
        }
        return status;

    case EWS_ROUTE_STATUS_CLOSE:
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        if (data->block.state > EWS_SESS_REQUEST_BEGIN) {
            finalize(sess);
            return EWS_ROUTE_STATUS_CLOSE;
        }
        return status;

    case EWS_ROUTE_STATUS_NOT_FOUND:
    case EWS_ROUTE_STATUS_FOUND:
        if (data->block.state != EWS_SESS_REQUEST_BEGIN) {
            LOGV("NOT_FOUND or FOUND status on state other than request begin");
            http_error(sess, 500, "Internal Server Error");
            return EWS_ROUTE_STATUS_ERROR;
        }
        return status;

    case EWS_ROUTE_STATUS_NEXT:
        switch (data->block.state) {
        case EWS_SESS_RESPONSE_BEGIN:
            data->block.state = EWS_SESS_RESPONSE_HEADER;
            break;

        case EWS_SESS_RESPONSE_HEADER:
            http_raw_send(sess, "\r\n", 2);
            data->block.state = EWS_SESS_RESPONSE_BODY;
            break;

        case EWS_SESS_RESPONSE_BODY:
            data->block.state = EWS_HTTP_FINALIZE;
            break;
        }
        return status;

    case EWS_ROUTE_STATUS_DONE:
        finalize(sess);
        return status;

    case EWS_ROUTE_STATUS_MORE:
        switch (data->block.state) {
        case EWS_SESS_RESPONSE_HEADER:
        case EWS_SESS_RESPONSE_BODY:
            return status;

        default:
            LOGV("MORE status on state other than response header or body");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
            if (data->block.state > EWS_SESS_REQUEST_BEGIN) {
                http_error(sess, 500, "Internal Server Error");
                return EWS_ROUTE_STATUS_ERROR;
            }
            return status;
        }
    }
}

static void parse_path(ews_sess_data_t *data)
{
    uint8_t *pi = (uint8_t *) data->path;
    uint8_t *po = (uint8_t *) data->path;

    while (*pi) {
        if (*pi == '%' && isxdigit(*(pi + 1)) && isxdigit(*(pi + 2))) {
            pi++;
            *po = (toupper(*pi) - (isdigit(*pi) ? '0' : 'A' - 10)) << 4;
            pi++;
            *po |= (toupper(*pi) - (isdigit(*pi) ? '0' : 'A' - 10));
            pi++;
            po++;
        } else if (*pi == '+') {
            pi++;
            *po++ = ' ';
        } else if (*pi == '/') {
            if (*(pi + 1) == '.') {
                if (*(pi + 2) == '\0' || *(pi + 2) == '/') {
                    pi += 2;
                    continue;
                } else if (*(pi + 2) == '.') {
                    if ((*pi + 3) == '\0' || (*pi + 3) == '/') {
                        pi += 3;
                        while (data->path_len > 0) {
                            po--;
                            data->path_len--;
                            if (*po == '/') {
                                break;
                            }
                        }
                        continue;
                    }
                }
            } else if (data->path_len > 0 && *(po - 1) == '/') {
                pi++;
                continue;
            }
            *po++ = *pi++;
        } else if (*pi == '?') {
            pi++;
            *po++ = '\0';
            data->query = (char *) pi;
            return;
        } else {
            *po++ = *pi++;
        }
        data->path_len++;
    }
    if (data->query) {
        data->query_len = strlen(data->query);
    }
}

static bool request_begin(ews_sess_t *sess)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ews_http_request_t *request = &data->block.request;
    ews_sock_t *sock = sess->sock;

    char *p, *version = NULL;

    request->buf = &data->buf[data->bufpos];
    request->buflen = find(request->buf, data->buflen, "\r\n");
    if (request->buflen < 0) {
        if (data->bufpos == 0 &&
                data->buflen >= sizeof(data->buf) - 1) {
            http_error(sess, 414, "URI Too Long");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        };
        return true;
    }
    request->buf[request->buflen] = '\0';
    data->buflen -= request->buflen + 2;
    data->bufpos += request->buflen + 2;

    /// parse request
    sess->data.path = NULL;
    p = (char *) request->buf;
    while (*p) {
        if (isspace(*p)) {
            *p++ = '\0';
            while (isspace(*p)) {
                p++;
            }
            if (sess->data.path == NULL) {
                sess->data.path = p;
            } else if (version == NULL) {
                version = p;
            }
        }
        p++;
    }
    if (sess->data.path == NULL) {
        http_error(sess, 400, "Bad Request");
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return true;
    }

    p = (char *) request->buf;
    while (*p) {
        *p = toupper(*p);
        p++;
    }

    const struct methods {
        const char *name;
        uint8_t id;
    } methods[] = {
        {"GET",     EWS_SESS_METHOD_GET},
        {"POST",    EWS_SESS_METHOD_POST},
        {"OPTIONS", EWS_SESS_METHOD_OPTIONS},
        {"HEAD",    EWS_SESS_METHOD_HEAD},
#if CONFIG_EWS_RARE_METHODS
        {"CONNECT", EWS_SESS_METHOD_CONNECT},
        {"DELETE",  EWS_SESS_METHOD_DELETE},
        {"PATCH",   EWS_SESS_METHOD_PATCH},
        {"PUT",     EWS_SESS_METHOD_PUT},
        {"TRACE",   EWS_SESS_METHOD_TRACE},
#endif
        {NULL,      EWS_SESS_METHOD_OTHER},
    };

    const struct methods *method = methods;
    while (method->name != NULL) {
        if (strcmp((char *) request->buf, method->name) == 0) {
            break;
        }
        method++;
    }
    sess->data.method = method->id;

    parse_path(&sess->data);

    /// determine HTTP version
    if (version) {
        p = version;
        while (*p) {
            *p = toupper(*p);
            p++;
        }

        if (strcmp(version, "HTTP/1.1") == 0) {
            data->block.version = EWS_HTTP_VERSION_11;
            data->block.flags |= EWS_HTTP_FLAGS_KEEPALIVE;
        } else if (strcmp(version, "HTTP/1.0") == 0) {
            data->block.version = EWS_HTTP_VERSION_10;
        } else {
            http_error(sess, 505, "HTTP Version Not Supported");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
            return true;
        }
    }

    LOGV("#%d %s %s", sock->fd, request->buf, sess->data.path);

    /// find a route that both matches the path and approves the request
    data->block.route = sock->ews->route_first;
    while (data->block.route != NULL) {
        if (fnmatch(data->block.route->pattern, sess->data.path)) {
            ews_route_status_t status = call_handler(sess);
            if (status == EWS_ROUTE_STATUS_FOUND) {
                break;
            } else if (status != EWS_ROUTE_STATUS_NOT_FOUND) {
                return true;
            }
        }
        data->block.route = data->block.route->next;
    }

    /// as a last resort, return a 404
    if (data->block.route == NULL) {
        data->block.route = &ews_route_404;
        call_handler(sess);
    }

    if (data->block.version == EWS_HTTP_VERSION_09) {
        data->block.state = EWS_SESS_RESPONSE_BEGIN;
    } else {
        data->block.state = EWS_SESS_REQUEST_HEADER;
    }

    return false;
}

static bool request_header(ews_sess_t *sess)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ews_http_request_t *request = &data->block.request;
    ews_sock_t *sock = sess->sock;

    request->buf = &data->buf[data->bufpos];
    request->buflen = find(request->buf, data->buflen, "\r\n");
    if (request->buflen < 0) {
        if (data->bufpos == 0 &&
                data->buflen >= sizeof(data->buf) - 1) {
            http_error(sess, 431, "Request Header Fields Too Large");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        };
        return true;
    }
    request->buf[request->buflen] = '\0';
    data->buflen -= request->buflen + 2;
    data->bufpos += request->buflen + 2;

    if (request->buflen == 0) {
        data->block.state = EWS_SESS_RESPONSE_BEGIN;
    } else {
        sess->data.name = (char *) request->buf;
        sess->data.name_len = find(request->buf, request->buflen, ": ");
        if (sess->data.name_len < 1 || sess->data.name_len >= request->buflen) {
            http_error(sess, 400, "Invalid Header");
            sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
            return true;
        }
        request->buf[sess->data.name_len] = '\0';
        sess->data.value = (char *) &request->buf[sess->data.name_len + 1];
        while (isspace(*sess->data.value)) {
            sess->data.value++;
        }
        sess->data.value_len =
                (char *) &request->buf[request->buflen] - sess->data.value;
        call_handler(sess);
    }

    return false;
}

static bool request_body(ews_sess_t *sess)
{
    // ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    // ews_http_request_t *request = &data->block.request;

    call_handler(sess);

    return true;
}

static void response_begin(ews_sess_t *sess)
{
    // ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    // ews_http_request_t *request = &data->block.request;

    call_handler(sess);
}

static void response_header(ews_sess_t *sess)
{
    // ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    // ews_http_request_t *request = &data->block.request;

    call_handler(sess);
}

static void response_body(ews_sess_t *sess)
{
    // ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    // ews_http_request_t *request = &data->block.request;

    call_handler(sess);
}

static void finalize(ews_sess_t *sess)
{
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ews_sock_t *sock = sess->sock;

    if (data->block.state == EWS_SESS_REQUEST_BEGIN) {
        return;
    }

    if (data->block.route) {
        data->block.state = EWS_HTTP_FINALIZE;
        data->block.state_count = 0;
        data->block.route->handler(sess, data->block.state);
    }

    if (!(data->block.flags & EWS_HTTP_FLAGS_KEEPALIVE)) {
        sock->ops->shutdown(sock);
    }

    memset(&data->block, 0, sizeof(data->block));
}

static void on_connect(ews_sock_t *sock)
{
    sock->ops->set_block(sock, false);
    sock->flags |= EWS_SOCK_FLAG_PROTO_HTTP | EWS_SOCK_FLAG_CONNECTED;

    ews_http_data_t *data = calloc(1, sizeof(*data));
    sock->user = &data->sess;
    data->sess.sock = sock;
    data->sess.ops = &http_sess_ops;
}

static void on_close(ews_sock_t *sock)
{
    ews_sess_t *sess = (ews_sess_t *) sock->user;
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    finalize(sess);
    free(data);
    sock->ops->close(sock);
}

static bool want_read(ews_sock_t *sock)
{
    ews_sess_t *sess = (ews_sess_t *) sock->user;
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    if ((data->block.state & 0x30) == 0x00) {
        return true;
    }

    return false;
}

static bool want_write(ews_sock_t *sock)
{
    ews_sess_t *sess = (ews_sess_t *) sock->user;
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    if ((data->block.state & 0x30) == 0x10) {
        return true;
    }

    return false;
}

static void do_read(ews_sock_t *sock)
{
    ews_sess_t *sess = (ews_sess_t *) sock->user;
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);
    ssize_t ret;

again:
    ret = sock->ops->recv(sock, data->buf + data->buflen,
            sizeof(data->buf) - data->buflen);
    if (ret <= 0) {
        return;
    }
    data->buflen += ret;

    const bool (*funcs[])(ews_sess_t *sess) = {
        request_begin,
        request_header,
        request_body,
    };

    while (data->buflen > 0) {
        if (funcs[data->block.state & 0xf](sess)) {
            if (sock->flags & EWS_SOCK_FLAG_PEND_CLOSE) {
                return;
            }
            goto done;
        }
    }

done:
    if (data->buflen == sizeof(data->buf)) {
        sock->flags |= EWS_SOCK_FLAG_PEND_CLOSE;
        return;
    }

    if (data->bufpos > 0) {
        memmove(data->buf, &data->buf[data->bufpos], data->buflen);
        data->bufpos = 0;
    }

    if (sock->ops->avail(sock) > 0) {
        goto again;
    }
}

static void do_write(ews_sock_t *sock)
{
    ews_sess_t *sess = (ews_sess_t *) sock->user;
    ews_http_data_t *data = container_of(sess, ews_http_data_t, sess);

    const void (*funcs[])(ews_sess_t *sess) = {
        response_begin,
        response_header,
        response_body,
    };

    funcs[data->block.state & 0xf](sess);
}

const ews_sock_evt_t http_sock_evt = {
    .on_connect = on_connect,
    .on_close = on_close,
    .want_read = want_read,
    .want_write = want_write,
    .do_read = do_read,
    .do_write = do_write,
};

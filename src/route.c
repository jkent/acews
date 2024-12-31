// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#include "ews.h"
#include "http.h"
#include "route.h"
#include "server.h"
#include "socket.h"


static bool ews_route_vappend(ews_t *ews, const char *pattern,
        ews_handler_t handler, int argc, va_list args)
{
    ews_route_t *route = calloc(1, sizeof(*route) + sizeof(void *) * argc);
    if (route == NULL) {
        return false;
    }

    if (ews->route_first == NULL) {
        ews->route_first = route;
    } else {
        ews->route_last->next = route;
    }
    ews->route_last = route;

    route->pattern = pattern;
    route->handler = handler;
    route->argc = argc;
    for (int i = 0; i < argc; i++) {
        route->argv[i] = va_arg(args, void *);
    }

    return true;
}

bool ews_routes_append(ews_t *ews, const char *pattern,
        ews_handler_t handler, int argc, ...)
{
    va_list args;
    bool ret;

    va_start(args, argc);
    ret = ews_route_vappend(ews, pattern, handler, argc, args);
    va_end(args);

    return ret;
}

void ews_routes_clear(ews_t *ews)
{
    assert(ews != NULL);

    ews_route_t *route = ews->route_first;
    while (route) {
        ews_route_t *next = route->next;
        ews->route_first = next;
        free(route);
        route = next;
    }
    ews->route_last = NULL;
}

ews_status_t ews_route_404_handler(ews_http_t *http)
{
    switch (http->sess->state) {
    case EWS_STATE_REQ:
        return EWS_STATUS_MATCH;

    case EWS_STATE_RSP:
        http->ops->start_rsp(http, 404, "Not Found");
        http->ops->send_hdr(http, "Content-Length", "18");
        return EWS_STATUS_NEXT;

    case EWS_STATE_RSP_BDY:
        http->ops->send(http, "<h1>Not Found</h1>", 18);
        return EWS_STATUS_DONE;
    }

    return EWS_STATUS_SKIP;
}

const ews_route_t ews_route_404 = {
    .handler = ews_route_404_handler,
};

ews_status_t ews_routes_test_handler(ews_http_t *http)
{
    const char *name, *value;
    char buf[1024];
    int ret;

    switch (http->sess->state) {
    case EWS_STATE_REQ:
        while (http->ops->get_hdr(http, &name, &value) > 0) {
            printf("hdr: %s: %s\n", name, value);
        }
        return EWS_STATUS_MATCH;

    case EWS_STATE_REQ_BDY:
        while ((ret = http->ops->recv(http, buf, sizeof(buf))) > 0) {
            printf("bdy: (%d)\"%.*s\"\n", ret, (int) ret, buf);
        }
        break;

    case EWS_STATE_REQ_MP:
        while (http->ops->get_hdr(http, &name, &value) > 0) {
            printf("mp_hdr: %s: %s\n", name, value);
        }
        break;

    case EWS_STATE_REQ_MP_BDY:
        while ((ret = http->ops->recv(http, buf, sizeof(buf))) > 0) {
            printf("mp_bdy: (%d)\"%.*s\"\n", ret, (int) ret, buf);
        }
        break;

    case EWS_STATE_RSP:
        http->ops->start_rsp(http, 200, "OK");
        http->ops->send_hdr(http, "Content-Length", "12");
        // http->ops->send_hdr(http, "Transfer-Encoding", "chunked");
        break;

    case EWS_STATE_RSP_BDY:
        http->ops->send(http, "Hello world!", 12);
        break;
    }

    return EWS_STATUS_NEXT;
}

typedef struct stdio_get_data stdio_get_data_t;
struct stdio_get_data {
    FILE *file;
    long size;
};

ews_status_t ews_routes_stdio_get_handler(ews_http_t *http)
{
    stdio_get_data_t *data = (stdio_get_data_t *) http->user;
    const char *name, *value;
    char buf[1024];
    struct stat st;
    int ret, end;

    switch (http->sess->state) {
    case EWS_STATE_REQ:
        while (http->ops->get_hdr(http, &name, &value)) {
            if (strcmp(name, ":method") == 0) {
                if (strcmp(value, "GET") != 0) {
                    return EWS_STATUS_NOMATCH;
                }
            }
            if (strcmp(name, ":x-path") == 0) {
                snprintf(buf, sizeof(buf), "%s%s",
                        (const char *) http->route->argv[0], value);
                ret = stat(buf, &st);
                if (ret < 0 || !S_ISREG(st.st_mode)) {
                    end = strlen(value);
                    if (end > 0) {
                        end--;
                    }
                    if (value[end] == '/') {
                        snprintf(buf, sizeof(buf), "%s%sindex.html",
                                (const char *) http->route->argv[0],
                                value);
                    } else {
                        snprintf(buf, sizeof(buf), "%s%s/index.html",
                                (const char *) http->route->argv[0],
                                value);
                    }
                    ret = stat(buf, &st);
                }
                if (ret < 0 || !S_ISREG(st.st_mode)) {
                    return EWS_STATUS_NOMATCH;
                }
                data = malloc(sizeof(stdio_get_data_t));
                if (!data) {
                    return EWS_STATUS_ERROR;
                }
                data->size = st.st_size;
                data->file = fopen(buf, "rb");
                if (!data->file) {
                    free(data);
                    return EWS_STATUS_ERROR;
                }
                http->user = data;
                return EWS_STATUS_MATCH;
            }
        }
        break;

    case EWS_STATE_RSP:
        http->ops->start_rsp(http, 200, "OK");
        http->ops->sendf_hdr(http, "Content-Length", "%d", data->size);
        break;

    case EWS_STATE_RSP_BDY:
        ret = fread(buf, 1, sizeof(data), data->file);
        if (ret > 0) {
            http->ops->send(http, buf, ret);
            return EWS_STATUS_MORE;
        }
        fclose(data->file);
        if (ret < 0) {
            return EWS_STATUS_ERROR;
        }
        return EWS_STATUS_DONE;

    case EWS_STATE_FIN:
        free(data);
        http->user = NULL;
        break;
    }

    return EWS_STATUS_SKIP;
}

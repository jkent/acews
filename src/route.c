// SPDX-License-Identifier: MIT
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "route.h"
#include "ews.h"
#include "server.h"
#include "socket.h"


static bool ews_route_vappend(ews_t *ews, const char *pattern,
        ews_route_handler_t handler, size_t argc, va_list args)
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
    for (size_t i = 0; i < argc; i++) {
        route->argv[i] = va_arg(args, void *);
    }

    return true;
}

bool ews_route_append(ews_t *ews, const char *pattern,
        ews_route_handler_t handler, size_t argc, ...)
{
    va_list args;
    bool ret;

    va_start(args, argc);
    ret = ews_route_vappend(ews, pattern, handler, argc, args);
    va_end(args);

    return ret;
}

void ews_route_clear(ews_t *ews)
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

ews_route_status_t ews_route_404_handler(ews_sess_t *sess,
        ews_sess_state_t state)
{
    switch (state) {
    case EWS_SESS_REQUEST_BEGIN:
        return EWS_ROUTE_STATUS_FOUND;

    case EWS_SESS_REQUEST_HEADER:
    case EWS_SESS_REQUEST_BODY:
        return EWS_ROUTE_STATUS_NEXT;

    case EWS_SESS_RESPONSE_BEGIN:
        sess->ops->error(sess, 404, "Not Found");
    default:
        return EWS_ROUTE_STATUS_DONE;
    }
}

const ews_route_t ews_route_404 = {
    .handler = ews_route_404_handler,
};

ews_route_status_t ews_route_test_handler(ews_sess_t *sess,
        ews_sess_state_t state)
{
    switch (state) {
    case EWS_SESS_REQUEST_BEGIN:
        return EWS_ROUTE_STATUS_FOUND;

    case EWS_SESS_REQUEST_HEADER:
        printf("header: (%lu)%s: (%lu)%s\n", sess->data.name_len,
                sess->data.name, sess->data.value_len, sess->data.value);
        return EWS_ROUTE_STATUS_NEXT;

    case EWS_SESS_REQUEST_BODY:
        printf("body: (%lu)\"%.*s\"\n", sess->data.chunk_len,
                (int) sess->data.chunk_len, sess->data.chunk);
        sess->ops->recv(sess, NULL, sess->data.chunk_len);
        return EWS_ROUTE_STATUS_NEXT;

    case EWS_SESS_RESPONSE_BEGIN:
        sess->ops->status(sess, 200, "OK");
        return EWS_ROUTE_STATUS_NEXT;

    case EWS_SESS_RESPONSE_HEADER:
        //sess->ops->header(sess, "Content-Length", "12");
        sess->ops->header(sess, "Transfer-Encoding", "chunked");
        return EWS_ROUTE_STATUS_NEXT;

    case EWS_SESS_RESPONSE_BODY:
        sess->ops->send(sess, "Hello world!", 12);
    default:
        return EWS_ROUTE_STATUS_DONE;
    }
}

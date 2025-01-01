// SPDX-License-Identifier: MIT
#pragma once

#include <stdarg.h>

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#include "ews.h"


typedef struct ews_route ews_route_t;

struct ews_route {
    ews_route_t *next;
    const char *pattern;
    ews_handler_t handler;
    size_t argc;
    void *argv[0];
};

extern const ews_route_t ews_route_404;

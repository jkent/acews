// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>


bool ews_fnmatch(const char *pattern, const char *string);
ssize_t ews_find(const char *buf, size_t hlen, const char *s);
ssize_t ews_findp(const char *buf, size_t hlen, const char *s);
void ews_parse_uri(const char *uri, char *path, size_t *path_len,
        const char **query, size_t *query_len);
void ews_hexdump(const char *buf, size_t len);

// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>


bool fnmatch(const char *pattern, const char *string);
int find(const char *buf, int hlen, const char *s);
int findp(const char *buf, int hlen, const char *s);
void parse_uri(const char *uri, char *path, const char **query);

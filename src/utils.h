// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>


bool fnmatch(const char *pattern, const char *string);
ssize_t find(const uint8_t *buf, size_t hlen, const char *s);
ssize_t findp(const uint8_t *buf, size_t hlen, const char *s);

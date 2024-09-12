// SPDX-License-Identifier: MIT
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "utils.h"


bool fnmatch(const char *pattern, const char *string)
{
    const char *p = pattern;
    const char *pe = p + strlen(pattern);
    const char *pl = NULL;
    const char *s = string;
    const char *se = s + strlen(string);

    while (true) {
        if (*p == '*') {
            p++;
            if (p == pe) {
                return true;
            }
            pl = p;
        }
        if (p == pe && s == se) {
            return true;
        } else if (p == pe || s == se) {
            return false;
        }
        if (*p == *s || *p == '?') {
            p++;
            s++;
        } else if (pl) {
            p = pl;
            s++;
        } else {
            return false;
        }
    }
}

ssize_t find(const uint8_t *buf, size_t hlen, const char *s)
{
    size_t nlen = strlen(s);
    size_t i = 0, j = 0;

    if (hlen < nlen) {
        return -1;
    }

    while (i <= hlen - nlen) {
        if (buf[i + j] == s[j]) {
            if (j++ == nlen - 1) {
                return i;
            }
        } else {
            i++;
            j = 0;
        }
    }
    return -1;
}

ssize_t findp(const uint8_t *buf, size_t hlen, const char *s)
{
    size_t nlen = strlen(s);
    size_t i = 0, j = 0;

    while (i <= hlen) {
        if (buf[i + j] == s[j]) {
            if (j++ == nlen - 1 || i + j == hlen) {
                return i;
            }
        } else {
            i++;
            j = 0;
        }
    }
    return -1;
}

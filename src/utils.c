// SPDX-License-Identifier: MIT
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
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

ssize_t find(const char *buf, size_t hlen, const char *s)
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

ssize_t findp(const char *buf, size_t hlen, const char *s)
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

void parse_uri(const char *uri, char *path, size_t *path_len,
        const char **query, size_t *query_len)
{
    const char *pi = uri;
    char *po = path;

    *path_len = 0;
    *query_len = 0;
    *query = NULL;

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
                        while (*path_len > 0) {
                            po--;
                            (*path_len)--;
                            if (*po == '/') {
                                break;
                            }
                        }
                        continue;
                    }
                }
            } else if (*path_len > 0 && *(po - 1) == '/') {
                pi++;
                continue;
            }
            *po++ = *pi++;
        } else if (*pi == '?') {
            *query = pi++;
            break;
        } else {
            *po++ = *pi++;
        }
        (*path_len)++;
    }
    *po++ = '\0';

    if (*query) {
        po = (char *) *query;
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
            } else {
                *po++ = *pi++;
            }
            (*query_len)++;
        }
        *po = '\0';
    }
}

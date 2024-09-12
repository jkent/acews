// SPDX-License-Identifier: MIT
#pragma once


#define container_of(ptr, type, member) ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - offsetof(type, member) ); \
})

#define countof(a) (sizeof(a) / sizeof(*(a)))

#define MIN(a, b) ({ \
    const typeof(a) _a = (a); \
    const typeof(b) _b = (b); \
    _a < _b ? _a : _b; \
})

#define MAX(a, b) ({ \
    const typeof(a) _a = (a); \
    const typeof(b) _b = (b); \
    _a > _b ? _a : _b; \
})

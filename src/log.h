// SPDX-License-Identifier: MIT
#pragma once

#include <stdio.h>

#include "ews_config.h"


#if defined(CONFIG_EWS_LOG_LEVEL_NONE)
# define LOG_LEVEL LOG_NONE
#elif defined(CONFIG_EWS_LOG_LEVEL_FATAL)
# define LOG_LEVEL LOG_FATAL
#elif defined(CONFIG_EWS_LOG_LEVEL_ERROR)
# define LOG_LEVEL LOG_ERROR
#elif defined(CONFIG_EWS_LOG_LEVEL_WARN)
# define LOG_LEVEL LOG_WARN
#elif defined(CONFIG_EWS_LOG_LEVEL_INFO)
# define LOG_LEVEL LOG_INFO
#elif defined(CONFIG_EWS_LOG_LEVEL_DEBUG)
# define LOG_LEVEL LOG_DEBUG
#elif defined(CONFIG_EWS_LOG_LEVEL_VERBOSE)
# define LOG_LEVEL LOG_VERBOSE
#else
# define LOG_LEVEL LOG_WARN
#endif

enum {
    LOG_NONE,
    LOG_FATAL,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_VERBOSE
};

#define LOG_COLOR(c)  "\033[" c "m"
#define LOG_COLOR_F   LOG_COLOR("30;41")
#define LOG_COLOR_E   LOG_COLOR("31")
#define LOG_COLOR_W   LOG_COLOR("33")
#define LOG_COLOR_I   LOG_COLOR("32")
#define LOG_COLOR_D
#define LOG_COLOR_V

#define LOG_FMT(l, fmt) LOG_COLOR_ ## l #l " %s: " fmt LOG_COLOR("0") "\n"

#define LOGF(fmt, ...) if (LOG_LEVEL >= LOG_FATAL)   { fprintf(stderr, LOG_FMT(F, fmt), __func__, ##__VA_ARGS__); }; exit(EXIT_FAILURE)
#define LOGE(fmt, ...) if (LOG_LEVEL >= LOG_ERROR)   { fprintf(stderr, LOG_FMT(E, fmt), __func__, ##__VA_ARGS__); }
#define LOGW(fmt, ...) if (LOG_LEVEL >= LOG_WARN)    { fprintf(stderr, LOG_FMT(W, fmt), __func__, ##__VA_ARGS__); }
#define LOGI(fmt, ...) if (LOG_LEVEL >= LOG_INFO)    { fprintf(stderr, LOG_FMT(I, fmt), __func__, ##__VA_ARGS__); }
#define LOGD(fmt, ...) if (LOG_LEVEL >= LOG_DEBUG)   { fprintf(stderr, LOG_FMT(D, fmt), __func__, ##__VA_ARGS__); }
#define LOGV(fmt, ...) if (LOG_LEVEL >= LOG_VERBOSE) { fprintf(stderr, LOG_FMT(V, fmt), __func__, ##__VA_ARGS__); }

#define TRACE() LOGV("%s:%d", __FILE__, __LINE__)
#define FTRACE(fmt, ...) LOGV("%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

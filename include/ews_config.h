// SPDX-License-Identifier: MIT
#pragma once

#if defined(ESP_PLATFORM)
# include "sdkconfig.h"
#endif


#if !defined(CONFIG_EWS_USE_IPV6)
# if defined(ESP_PLATFORM)
#  define CONFIG_EWS_USE_IPV6 CONFIG_LWIP_IPV6
# else
#  define CONFIG_EWS_USE_IPV6 1
# endif
#endif

#if !defined(CONFIG_EWS_SOCKET_TIMEOUT)
# define CONFIG_EWS_SOCKET_TIMEOUT 100
#endif

#if !defined(CONFIG_EWS_HTTP_CLIENTS)
# define CONFIG_EWS_HTTP_CLIENTS 2
#endif

#if !defined(CONFIG_EWS_HTTP_BACKLOG_DFLT)
# define CONFIG_EWS_HTTP_BACKLOG_DFLT ((CONFIG_EWS_HTTP_CLIENTS) * 3 / 2)
#endif

#if !defined(CONFIG_EWS_HTTPS_CLIENTS)
# define CONFIG_EWS_HTTPS_CLIENTS 0
#endif

#if !defined(CONFIG_EWS_HTTPS_BACKLOG_DFLT)
# define CONFIG_EWS_HTTPS_BACKLOG_DFLT ((CONFIG_EWS_HTTPS_CLIENTS) * 3 / 2)
#endif

#if !defined(CONFIG_EWS_IDLE_TIMEOUT_DFLT)
# define CONFIG_EWS_IDLE_TIMEOUT_DFLT 15000
#endif

#if !defined(CONFIG_EWS_SESSION_BUFSIZE)
# define CONFIG_EWS_SESSION_BUFSIZE 2048
#endif

#if !defined(CONFIG_EWS_WORKER_STACK_SIZE)
# define CONFIG_EWS_WORKER_STACK_SIZE 4096
#endif

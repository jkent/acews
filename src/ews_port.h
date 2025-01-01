// SPDX-License-Identifier: MIT
#pragma once

#include "ews_config.h"

#define EWS_PRIVATE_DEFS
#if defined(ESP_PLATFORM)
# define FREERTOS_BASE "freertos"
# include "ews_port_freertos.h"
#else
# include "ews_port_linux.h"
#endif

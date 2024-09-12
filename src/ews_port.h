// SPDX-License-Identifier: MIT
#pragma once

#if defined(ESP_PLATFORM)
# define FREERTOS_BASE "freertos"
# include "ews_port_freertos.h"
#else
# include "ews_port_linux.h"
#endif

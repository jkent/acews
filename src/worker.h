// SPDX-License-Identifier: MIT
#pragma once

#include "ews_port.h"


typedef struct ews_worker ews_worker_t;

struct ews_worker {
    ews_thread_t thread;
    ews_timer_t timer;
    bool shutdown;
};

bool ews_worker_init(ews_worker_t *worker);
void ews_worker_destroy(ews_worker_t *worker);

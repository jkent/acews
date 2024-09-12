// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>

#define INC_STR(x) __INC_STR(x)
#define __INC_STR(x) #x
#define INC_PATH_CAT(x, y) x/y
#ifdef FREERTOS_BASE
# define FREERTOS_INC(file) INC_STR(INC_PATH_CAT(FREERTOS_BASE, file))
#else
# define FREERTOS_INC(file) INC_STR(file)
#endif

#include FREERTOS_INC(FreeRTOS.h)
#include FREERTOS_INC(semphr.h)
#include FREERTOS_INC(task.h)
#include FREERTOS_INC(timers.h)

#include "log.h"
#include "macros.h"


////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_timing Portable timing
/// @{

/// delay a sepecified number of milliseconds
/// @param[in] ms number of milliseconds to delay
static inline void ews_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/// get millisecond time
/// @returns milliseconds, for use in time delta calculations
static inline uint32_t ews_time_ms(void)
{
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_mutexes Portable mutexes
/// @{

/// mutex type
typedef struct ews_mutex ews_mutex_t;

/// @internal
/// mutex struct
struct ews_mutex {
    bool recursive;
    StaticSemaphore_t mutex;
};
/// @endinternal

/// initialize a mutex
/// @param[in] mutex pointer to ews_mutex
/// @param[in] recursive @a true for recursive, @a false otherwise
static inline void ews_mutex_init(ews_mutex_t *mutex, bool recursive)
{
    assert(mutex != NULL);

    mutex->recursive = recursive;
    if (recursive) {
        xSemaphoreCreateRecursiveMutexStatic(&mutex->mutex);
    } else {
        xSemaphoreCreateMutexStatic(&mutex->mutex);
    }
}

/// tear down a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_destroy(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    /* nothing to do */
}

/// lock a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_lock(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    if (mutex->recursive) {
        xSemaphoreTakeRecursive((SemaphoreHandle_t) &mutex->mutex,
                portMAX_DELAY);
    } else {
        xSemaphoreTake((SemaphoreHandle_t) &mutex->mutex, portMAX_DELAY);
    }
}

/// unlock a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_unlock(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    if (mutex->recursive) {
        xSemaphoreGiveRecursive((SemaphoreHandle_t) &mutex->mutex);
    } else {
        xSemaphoreGive((SemaphoreHandle_t) &mutex->mutex);
    }
}

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_semaphores Portable semaphores
/// @{

/// semaphore type
typedef struct ews_semaphore ews_semaphore_t;

/// @internal
/// semaphore struct
struct ews_semaphore {
    StaticSemaphore_t semaphore;
};
/// @endinternal

/// initialize a semaphore
/// @param[in] semaphore pointer to ews_semaphore
/// @param[in] max maximum semaphore value
/// @param[in] initial initial semaphore value
/// @return 1 on success, 0 on error
static inline int ews_semaphore_init(ews_semaphore_t *semaphore, uint32_t max,
        uint32_t initial)
{
    assert(semaphore != NULL);

    xSemaphoreCreateCountingStatic(max, initial, &semaphore->semaphore);
    return 1;
}

/// tear down a semaphore
/// @param[in] semaphore pointer to ews_semaphore
static inline void ews_sempaphore_destroy(ews_semaphore_t *semaphore)
{
    assert(semaphore != NULL);

    /* nothing to do */
}

/// take on a semapore
/// @param semaphore pointer to ews_semaphore
/// @param timeout_ms timeout; 0 for no timeout, @a UINT32_MAX to wait forever
/// @return 1 on success, 0 on timeout or error
static inline int ews_semaphore_take(ews_semaphore_t *semaphore,
        uint32_t timeout_ms)
{
    assert(semaphore != NULL);

    return xSemaphoreTake((SemaphoreHandle_t) &semaphore->semaphore,
            pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/// give on a semaphore
/// @param semaphore pointer to ews_semaphore
/// @return 1 on sucess, 0 on max value reached or error
static inline int ews_semaphore_give(ews_semaphore_t *semaphore)
{
    assert(semaphore != NULL);

    return xSemaphoreGive((SemaphoreHandle_t) &semaphore->semaphore) == pdTRUE;
}

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_threads Portable threads
/// @{

/// thread typedef
typedef struct ews_thread ews_thread_t;

/// thread handler typedef
typedef void (*ews_thread_func_t)(void *arg);

/// @internal
/// thread struct
struct ews_thread {
    TaskHandle_t handle;
    ews_thread_func_t func;
    void *arg;
};

static inline void ews_thread_destroy(ews_thread_t *thread);

/// non-api thread wrapper function
/// @param[in] arg ews_thread instance
static void thread_wrapper(void *arg)
{
    ews_thread_t *thread = (ews_thread_t *) arg;

    thread->func(thread->arg);

    ews_thread_destroy(thread);
}
/// @endinternal

/// initialize and start a thread
/// @param[in] thread pointer to ews_thread
/// @param[in] func thread function
/// @param[in] arg thread argument
/// @param[in] stack_words size of the stack in number of machine words
/// @return 1 on success, 0 on error
static inline int ews_thread_init(ews_thread_t *thread, ews_thread_func_t func,
        void *arg, size_t stack_words)
{
    assert(thread != NULL);

    thread->func = func;
    thread->arg = arg;

    return xTaskCreate(thread_wrapper, "ews", stack_words, thread,
            tskIDLE_PRIORITY + 1, &thread->handle) == pdPASS;
}

/// tear down a thread
/// @param[in] thread pointer to ews_thread
static inline void ews_thread_destroy(ews_thread_t *thread)
{
    assert(thread != NULL);

    if (thread == NULL) {
        vTaskDelete(NULL);
    } else {
        vTaskDelete(thread->handle);
    }
}

/// @}
////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_timers Portable timers
/// @{

/// timer typedef
typedef struct ews_timer ews_timer_t;

/// timer handler typedef
typedef void (*ews_timer_handler_t)(void *arg);

/// @internal
/// timer struct
struct ews_timer {
    StaticTimer_t timer;
    TimerHandle_t handle;
    ews_timer_handler_t func;
    void *arg;
};

/// non-api timer wrapper function
/// @param val
static void timer_wrapper(TimerHandle_t handle)
{
    ews_timer_t *timer = (ews_timer_t *) pvTimerGetTimerID(handle);
    timer->func(timer->arg);
}
/// @endinternal

/// initialize a timer
/// @param[in] timer pointer to ews_timer
/// @param[in] period_ms timer period in millisecondsd
/// @param[in] autoreload @a true for periodic, @a false for one-shot
/// @param[in] func timer handler function
/// @param[in] arg timer handler argument
/// @return 1 on success or 0 on failure
static inline int ews_timer_init(ews_timer_t *timer, int period_ms,
        bool autoreload, ews_timer_handler_t func, void *arg)
{
    assert(timer != NULL);

    timer->handle = xTimerCreateStatic("ews", pdMS_TO_TICKS(period_ms),
            autoreload, timer, timer_wrapper, &timer->timer);
    if (timer->handle == NULL) {
        return 0;
    }

    timer->func = func;
    timer->arg = arg;
    return 1;
}

/// tear down a timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_destroy(ews_timer_t *timer)
{
    assert(timer != NULL);

    xTimerDelete(timer->handle, portMAX_DELAY);
}

/// start/arm timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_start(ews_timer_t *timer)
{
    assert(timer != NULL);

    xTimerStart(timer->handle, portMAX_DELAY);
}

/// stop/cancel timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_stop(ews_timer_t *timer)
{
    assert(timer != NULL);

    xTimerStop(timer->handle, portMAX_DELAY);
}

/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
} // extern "C"
#endif

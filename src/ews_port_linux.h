// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "macros.h"


////////////////////////////////////////////////////////////////////////////////
/// @defgroup ews_timing Portable timing
/// @{

/// delay a sepecified number of milliseconds
/// @param[in] ms number of milliseconds to delay
static inline void ews_delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

/// get millisecond time
/// @returns milliseconds, for use in time delta calculations
static inline uint32_t ews_time_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint32_t) tv.tv_sec * 1000ULL) + ((uint32_t) tv.tv_usec / 1000ULL);
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
    pthread_mutex_t mutex;
};
/// @endinternal

/// initialize a mutex
/// @param[in] mutex pointer to ews_mutex
/// @param[in] recursive @a true for recursive, @a false otherwise
static inline void ews_mutex_init(ews_mutex_t *mutex, bool recursive)
{
    assert(mutex != NULL);

    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr,
            recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&mutex->mutex, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);
}

/// tear down a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_destroy(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    pthread_mutex_destroy(&mutex->mutex);
}

/// lock a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_lock(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    pthread_mutex_lock(&mutex->mutex);
}

/// unlock a mutex
/// @param[in] mutex pointer to ews_mutex
static inline void ews_mutex_unlock(ews_mutex_t *mutex)
{
    assert(mutex != NULL);

    pthread_mutex_unlock(&mutex->mutex);
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
    sem_t semaphore;
    int max;
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
    semaphore->max = max;
    if (sem_init(&semaphore->semaphore, 0, initial)) {
        return 1;
    }

    return 0;
}

/// tear down a semaphore
/// @param[in] semaphore pointer to ews_semaphore
static inline void ews_sempaphore_destroy(ews_semaphore_t *semaphore)
{
    assert(semaphore != NULL);

    sem_destroy(&semaphore->semaphore);
}

/// take on a semapore
/// @param semaphore pointer to ews_semaphore
/// @param timeout_ms timeout; 0 for no timeout, @a UINT32_MAX to wait forever
/// @return 1 on success, 0 on timeout or error
static inline int ews_semaphore_take(ews_semaphore_t *semaphore,
        uint32_t timeout_ms)
{
    assert(semaphore != NULL);

    if (timeout_ms == 0) {
        return sem_trywait(&semaphore->semaphore) == 0;
    } else if (timeout_ms == UINT32_MAX) {
        return sem_wait(&semaphore->semaphore) == 0;
    } else {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
            return -1;
        }
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        ts.tv_sec += ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;
        return sem_timedwait(&semaphore->semaphore, &ts) == 0;
    }
}

/// give on a semaphore
/// @param semaphore pointer to ews_semaphore
/// @return 1 on sucess, 0 on max value reached or error
static inline int ews_semaphore_give(ews_semaphore_t *semaphore)
{
    assert(semaphore != NULL);

    int num;
    sem_getvalue(&semaphore->semaphore, &num);
    if (num == semaphore->max) {
        return 0;
    }
    return sem_post(&semaphore->semaphore) == 0;
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
    pthread_t pthread;
    ews_thread_func_t func;
    void *arg;
};

static inline void ews_thread_destroy(ews_thread_t *thread);

/// non-api thread wrapper function
/// @param[in] arg ews_thread instance
/// @return @a NULL
static void *thread_wrapper(void *arg)
{
    ews_thread_t *thread = (ews_thread_t *) arg;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    thread->func(thread->arg);

    ews_thread_destroy(thread);

    return NULL;
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
    pthread_attr_t attr;

    assert(thread != NULL);

    thread->func = func;
    thread->arg = arg;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MAX(PTHREAD_STACK_MIN,
            stack_words * sizeof(size_t)));
    if (pthread_create(&thread->pthread, &attr, thread_wrapper, thread) < 0) {
        pthread_attr_destroy(&attr);
        LOGE("pthread_create failed");
        return 0;
    }
    pthread_attr_destroy(&attr);
    return 1;
}

/// tear down a thread
/// @param[in] thread pointer to ews_thread
static inline void ews_thread_destroy(ews_thread_t *thread)
{
    assert(thread != NULL);

    if (pthread_self() == thread->pthread) {
        sigset_t set;
        struct timespec timeout = { 0 };
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        sigtimedwait(&set, NULL, &timeout);
    }

    if (thread == NULL) {
        pthread_exit(NULL);
    } else {
        pthread_exit(&thread->pthread);
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
    timer_t handle;
    struct itimerspec its;
    ews_timer_handler_t func;
    void *arg;
};

/// non-api timer wrapper function
/// @param val
static void timer_wrapper(union sigval val)
{
    ews_timer_t *timer = (ews_timer_t *) val.sival_ptr;
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

    timer->its.it_value.tv_sec = period_ms / 1000;
    timer->its.it_value.tv_nsec = (period_ms % 1000) * 1000000;
    if (autoreload) {
        timer->its.it_interval.tv_sec = period_ms / 1000;
        timer->its.it_interval.tv_nsec = (period_ms % 1000) * 1000000;
    } else {
       timer->its.it_interval.tv_sec = 0;
       timer->its.it_value.tv_nsec = 0;
    }
    timer->func = func;
    timer->arg = arg;

    struct sigevent sev = {
        .sigev_notify = SIGEV_THREAD,
        .sigev_value.sival_ptr = timer,
        .sigev_notify_function = timer_wrapper,
        .sigev_notify_attributes = 0,
    };

    if (timer_create(CLOCK_MONOTONIC, &sev, &timer->handle) < 0) {
        return 0;
    }
    return 1;
}

/// tear down a timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_destroy(ews_timer_t *timer)
{
    assert(timer != NULL);

    timer_delete(timer->handle);
}

/// start/arm timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_start(ews_timer_t *timer)
{
    assert(timer != NULL);

    if (timer_settime(timer->handle, 0, &timer->its, NULL) < 0) {
        LOGE("timer_settime failed");
    }
}

/// stop/cancel timer
/// @param[in] timer pointer to ews_timer
static inline void ews_timer_stop(ews_timer_t *timer)
{
    assert(timer != NULL);

    struct itimerspec its = {};
    if (timer_settime(timer->handle, 0, &its, NULL) < 0) {
        LOGE("timer_settime failed");
    }
}

/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
} // extern "C"
#endif

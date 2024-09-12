// SPDX-License-Identifier: MIT
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>

#include "worker.h"
#include "server.h"
#include "socket.h"


static void worker_task(void *arg);
static void task_reaper(void *arg);

bool ews_worker_init(ews_worker_t *worker)
{
    return ews_thread_init(&worker->thread, worker_task, worker,
            CONFIG_EWS_WORKER_STACK_SIZE);
}

void ews_worker_destroy(ews_worker_t *worker)
{
    ews_timer_init(&worker->timer, 5000, false, task_reaper, worker);
    ews_timer_start(&worker->timer);
    worker->shutdown = true;
}

static void pre_select(ews_sock_t *sock, uint32_t now, int *fd_max,
        fd_set *rfds, fd_set *wfds)
{
    if (!(sock->flags & EWS_SOCK_FLAG_INUSE)) {
        return;
    }

    if (sock->connect) {
        sock->connect(sock);
        sock->connect = NULL;
    }

    if (sock->idle_timeout > 0) {
        if (now - sock->last_active > sock->idle_timeout) {
            LOGD("#%d idle timeout", sock->fd);
            if (sock->evt && sock->evt->on_close) {
                sock->evt->on_close(sock);
            } else {
                sock->ops->close(sock);
            }
            return;
        }
    }

    if (sock->flags & EWS_SOCK_FLAG_PEND_CLOSE) {
        if (sock->evt && sock->evt->on_close) {
            sock->evt->on_close(sock);
        } else {
            sock->ops->close(sock);
        }
        return;
    }

    if (!sock->evt) {
        return;
    }

    if (!(sock->flags & EWS_SOCK_FLAG_CONNECTED) && sock->evt->on_connect) {
        sock->evt->on_connect(sock);
    }

    if (sock->flags & EWS_SOCK_FLAG_CONNECTED) {
        if (sock->evt->want_read && sock->evt->want_read(sock)) {
            FD_SET(sock->fd, rfds);
            *fd_max = MAX(*fd_max, sock->fd);
        }
        if (sock->evt->want_write && sock->evt->want_write(sock)) {
            FD_SET(sock->fd, wfds);
            *fd_max = MAX(*fd_max, sock->fd);
        }
    }
}

static void post_select(ews_sock_t *sock, uint32_t now, fd_set *rfds,
        fd_set *wfds)
{
    if (sock->flags & EWS_SOCK_FLAG_CONNECTED) {
        if (FD_ISSET(sock->fd, rfds) && sock->evt->do_read) {
            sock->last_active = now;
            sock->evt->do_read(sock);
        }
        if (FD_ISSET(sock->fd, wfds) && sock->evt->do_write) {
            sock->last_active = now;
            sock->evt->do_write(sock);
        }
    }
}

static void worker_loop(ews_worker_t *worker)
{
    ews_t *ews = container_of(worker, ews_t, worker);
    struct timeval tv;
    fd_set rfds, wfds;
    uint32_t now;
    int fd_max = 0;
    int ret;

    now = ews_time_ms();

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

#if CONFIG_EWS_HTTP_CLIENTS > 0
    pre_select(&ews->http_listener.sock, now, &fd_max, &rfds, &wfds);
    for (int i = 0; i < countof(ews->http_client); i++) {
        ews_sock_t *sock = &ews->http_client[i].sock;
        pre_select(sock, now, &fd_max, &rfds, &wfds);
    }
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0
    pre_select(&ews->https_listener.sock, now, &fd_max, &rfds, &wfds);
    for (int i = 0; i < countof(ews->https_client); i++) {
        ews_sock_t *sock = &ews->https_client[i].sock;
        pre_select(sock, now, &fd_max, &rfds, &wfds);
    }
#endif

    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    ret = select(fd_max + 1, &rfds, &wfds, NULL, &tv);
    if (ret == 0) {
        return;
    }
    if (ret < 0) {
        LOGE("select failed");
        worker->shutdown = true;
    }

#if CONFIG_EWS_HTTP_CLIENTS > 0
    post_select(&ews->http_listener.sock, now, &rfds, &wfds);
    for (int i = 0; i < countof(ews->http_client); i++) {
        ews_sock_t *sock = &ews->http_client[i].sock;
        post_select(sock, now, &rfds, &wfds);
    }
#endif

#if CONFIG_EWS_HTTPS_CLIENTS > 0
    post_select(&ews->https_listener.sock, now, &rfds, &wfds);
    for (int i = 0; i < countof(ews->https_client); i++) {
        ews_sock_t *sock = &ews->https_client[i].sock;
        post_select(sock, now, &rfds, &wfds);
    }
#endif
}

static void worker_task(void *arg)
{
    ews_worker_t *worker = arg;

    while (!worker->shutdown) {
        worker_loop(worker);
    }

    ews_timer_stop(&worker->timer);
    ews_timer_destroy(&worker->timer);
    ews_thread_destroy(&worker->thread);
}

static void task_reaper(void *arg)
{
    ews_worker_t *worker = arg;

    ews_thread_destroy(&worker->thread);
    ews_timer_destroy(&worker->timer);
}

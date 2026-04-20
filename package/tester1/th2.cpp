/* $Id$
 *
 * Copyright (c) 2026, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file th1.cpp
 * @brief noname
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)

#include "priv_ev.h"
#ifdef __cplusplus
#  include <atomic>
#else
#  include <stdatomic.h>
#endif

#ifdef __cplusplus
//typedef std::atomic<int> atomic_int;
typedef std::atomic_int atomic_int;
#endif

typedef void (*task_fn)(void*);

typedef struct task {
    task_fn fn;
    void* arg;
    struct task* next;
} task_t;

typedef struct {
    pthread_t thread;

    pthread_mutex_t lock;
    pthread_cond_t cond;

    task_t* head;
    task_t* tail;

    atomic_int stop;     // request stop
    int running;         // thread created

    int drain;           // 1 = finish queued tasks before exit
} worker_t;

#define dump_argv(_argc, _argv) for (int i = 0; i < _argc; i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, _argc, _argv[i]); \
}

static void enqueue(worker_t* w, task_t* t)
{
    t->next = NULL;

    if (w->tail) {
        w->tail->next = t;
    } else {
        w->head = t;
    }
    w->tail = t;
}

static task_t* dequeue(worker_t* w)
{
    task_t* t = w->head;
    if (!t) return NULL;

    w->head = t->next;
    if (!w->head) w->tail = NULL;

    return t;
}

static void* worker_thread(void* arg)
{
    worker_t* w = (worker_t*)arg;

    pthread_mutex_lock(&w->lock);

    for (;;) {
        // wait until: task available OR stop requested
        while (!w->head && !atomic_load(&w->stop)) {
            pthread_cond_wait(&w->cond, &w->lock);
        }

        // stop logic
        if (atomic_load(&w->stop)) {
            if (!w->drain || !w->head) {
                break; // exit
            }
        }

        task_t* t = dequeue(w);

        pthread_mutex_unlock(&w->lock);

        if (t) {
            t->fn(t->arg);
            free(t);
        }

        pthread_mutex_lock(&w->lock);
    }

    pthread_mutex_unlock(&w->lock);
    return NULL;
}

int worker_init(worker_t* w, int drain)
{
    w->head = w->tail = NULL;
    w->running = 0;
    w->drain = drain;

    atomic_store(&w->stop, 0);

    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->cond, NULL);

    return 0;
}

int worker_start(worker_t* w)
{
    if (w->running) return 0;

    atomic_store(&w->stop, 0);

    if (pthread_create(&w->thread, NULL, worker_thread, w) != 0) {
        return -1;
    }

    w->running = 1;
    return 0;
}

int worker_submit(worker_t* w, task_fn fn, void* arg)
{
    task_t* t = (task_t*)malloc(sizeof(task_t));
    if (!t) return -1;

    t->fn = fn;
    t->arg = arg;

    pthread_mutex_lock(&w->lock);

    enqueue(w, t);

    pthread_cond_signal(&w->cond);

    pthread_mutex_unlock(&w->lock);

    return 0;
}

void worker_stop(worker_t* w)
{
    if (!w->running) return;

    atomic_store(&w->stop, 1);

    pthread_mutex_lock(&w->lock);
    pthread_cond_broadcast(&w->cond); // wake worker
    pthread_mutex_unlock(&w->lock);

    pthread_join(w->thread, NULL);

    w->running = 0;
}

int worker_restart(worker_t* w)
{
    worker_stop(w);
    return worker_start(w);
}

void worker_destroy(worker_t* w)
{
    worker_stop(w);

    pthread_mutex_lock(&w->lock);

    // free remaining tasks (if any)
    task_t* t = w->head;
    while (t) {
        task_t* next = t->next;
        free(t);
        t = next;
    }

    pthread_mutex_unlock(&w->lock);

    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->cond);
}

void my_task(void* arg)
{
    int v = *(int*)arg;
    printf("task %d\n", v);
}

int main(int argc, const char **argv) {
	int ret = -1;

	log_d("%s\n", aloe_version(NULL, 0));

	dump_argv(argc, argv)

    worker_t w;
    worker_init(&w, 1); // drain = 1

    worker_start(&w);

    for (int i = 0; i < 5; i++) {
        int* v = (int*)malloc(sizeof(int));
        *v = i;
        worker_submit(&w, my_task, v);
    }

    sleep(2);

    worker_stop(&w);
    worker_destroy(&w);

	return 0;
}


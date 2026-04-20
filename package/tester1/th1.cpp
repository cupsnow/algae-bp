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

typedef void (*task_fn)(void *arg);

typedef struct task {
	task_fn fn;
	void *arg;
	struct task *next;
} task_t;

typedef struct {
	pthread_t thread;

	pthread_mutex_t lock;
	pthread_cond_t cond;

	int stop;
	int running;

	task_t *head;
	task_t *tail;
} worker_t;

#define dump_argv(_argc, _argv) for (int i = 0; i < _argc; i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, _argc, _argv[i]); \
}

static void queue_push(worker_t *w, task_t *t)
		{
	t->next = NULL;
	if (w->tail) {
		w->tail->next = t;
	} else {
		w->head = t;
	}
	w->tail = t;
}

static task_t* queue_pop(worker_t *w)
		{
	task_t *t = w->head;
	if (!t) return NULL;

	w->head = t->next;
	if (!w->head)
		w->tail = NULL;

	return t;
}

static void* worker_thread(void *arg) {
	worker_t *w = (worker_t*)arg;

	pthread_mutex_lock(&w->lock);

	while (1) {
		while (!w->stop && w->head == NULL) {
			pthread_cond_wait(&w->cond, &w->lock);
		}

		if (w->stop && w->head == NULL) {
			break; // clean exit
		}

		task_t *task = queue_pop(w);

		pthread_mutex_unlock(&w->lock);

		if (task) {
			task->fn(task->arg);
			free(task);
		}

		pthread_mutex_lock(&w->lock);
	}

	w->running = 0;
	pthread_mutex_unlock(&w->lock);

	return NULL;
}

int worker_init(worker_t *w)
		{
	w->stop = 0;
	w->running = 0;
	w->head = w->tail = NULL;

	pthread_mutex_init(&w->lock, NULL);
	pthread_cond_init(&w->cond, NULL);

	return 0;
}

void worker_destroy(worker_t *w)
		{
	pthread_mutex_lock(&w->lock);

	task_t *t = w->head;
	while (t) {
		task_t *next = t->next;
		free(t);
		t = next;
	}

	pthread_mutex_unlock(&w->lock);

	pthread_mutex_destroy(&w->lock);
	pthread_cond_destroy(&w->cond);
}

int worker_start(worker_t *w)
		{
	pthread_mutex_lock(&w->lock);

	if (w->running) {
		pthread_mutex_unlock(&w->lock);
		return 0;
	}

	w->stop = 0;
	w->running = 1;

	pthread_mutex_unlock(&w->lock);

	if (pthread_create(&w->thread, NULL, worker_thread, w) != 0) {
		return -1;
	}

	return 0;
}

void worker_stop(worker_t *w)
		{
	pthread_mutex_lock(&w->lock);

	if (!w->running) {
		pthread_mutex_unlock(&w->lock);
		return;
	}

	w->stop = 1;
	pthread_cond_broadcast(&w->cond);

	pthread_mutex_unlock(&w->lock);

	pthread_join(w->thread, NULL);
}

int worker_restart(worker_t *w)
		{
	worker_stop(w);
	return worker_start(w);
}

int worker_submit(worker_t *w, task_fn fn, void *arg) {
	task_t *t = (task_t*)malloc(sizeof(task_t));
	if (!t) return -1;

	t->fn = fn;
	t->arg = arg;

	pthread_mutex_lock(&w->lock);

	if (w->stop) {
		pthread_mutex_unlock(&w->lock);
		free(t);
		return -1;
	}

	queue_push(w, t);

	pthread_cond_signal(&w->cond);

	pthread_mutex_unlock(&w->lock);

	return 0;
}

void print_task(void *arg) {
	int v = *(int*)arg;
	printf("task: %d\n", v);
}

int main(int argc, const char **argv) {
	int ret = -1;

	log_d("%s\n", aloe_version(NULL, 0));

	dump_argv(argc, argv)

	worker_t w;
	worker_init(&w);

	worker_start(&w);

	for (int i = 0; i < 5; i++) {
		int *arg = (int*)malloc(sizeof(int));
		*arg = i;
		worker_submit(&w, print_task, arg);
	}

	sleep(2);

	worker_stop(&w);
	worker_destroy(&w);

	return 0;
}


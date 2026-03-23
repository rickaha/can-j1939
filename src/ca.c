/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ca.h"
#include "time.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

/* STRUCTS */

typedef struct {
    uint32_t pgn;
    uint32_t tx_rate_ms;
    uint64_t last_tx_ms;
} pgn_task_t;

struct ca_t {
    volatile sig_atomic_t running;
    int sock;
    uint64_t name;
    uint8_t preferred_addr;
    uint8_t claimed_addr;
    ca_identity_t identity;
    sensor_values_t sensors;
    pthread_mutex_t sensors_mutex;
    pthread_mutex_t tx_mutex;
    pthread_cond_t tx_cond;
    pgn_request_t request_queue[REQUEST_QUEUE_SIZE];
    uint8_t request_queue_count;
    pthread_t tx_tid;
    pthread_t sensor_tid;
};

/* HELPERS */

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* TASKS */

static sensor_task_t sensor_tasks[] = {
    {.poll_rate_ms = 1000, .read = ambient_temp_read, .write = ambient_temp_write},
};

static const size_t sensor_tasks_count = sizeof(sensor_tasks) / sizeof(sensor_tasks[0]);

static pgn_task_t pgn_tasks[] = {
    {.pgn = PGN_65269, .tx_rate_ms = 1000},
};

static const size_t pgn_tasks_count = sizeof(pgn_tasks) / sizeof(pgn_tasks[0]);

/* SENSORS POLL */

static void sensors_poll(ca_t* ca) {
    uint64_t now_ms = get_time_ms();

    for (size_t i = 0; i < sensor_tasks_count; i++) {
        if (now_ms - sensor_tasks[i].last_poll_ms < sensor_tasks[i].poll_rate_ms)
            continue;

        /* Read from hardware into local buffer — no lock held during hardware access. */
        uint8_t buf[64];
        if (sensor_tasks[i].read(buf) < 0) {
            sensor_tasks[i].last_poll_ms = now_ms;
            continue;
        }

        /* Write into sensor_values_t under the mutex. */
        pthread_mutex_lock(&ca->sensors_mutex);
        sensor_tasks[i].write(&ca->sensors, buf);
        pthread_mutex_unlock(&ca->sensors_mutex);

        sensor_tasks[i].last_poll_ms = now_ms;
    }
}

/* THREADS */

static void* sensor_thread(void* arg) {
    ca_t* ca = (ca_t*)arg;

    printf("[sensor] Thread started. tid=%lu\n", pthread_self());

    while (ca->running) {
        sensors_poll(ca);
    }

    printf("[sensor] Thread exiting.\n");
    return NULL;
}

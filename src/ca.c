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
#include <stdlib.h>

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

static void* tx_thread(void* arg) {
    ca_t* ca = (ca_t*)arg;

    printf("[tx] Thread started. tid=%lu\n", pthread_self());

    while (ca->running) {
        /* Wait for RX signal or wake up every 10ms to service periodic PGNs. */
        pthread_mutex_lock(&ca->tx_mutex);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 10000000L; /* 10ms */
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&ca->tx_cond, &ca->tx_mutex, &deadline);

        /* Drain on-request queue. */
        while (ca->request_queue_count > 0) {
            ca->request_queue_count--;
            pgn_request_t req = ca->request_queue[ca->request_queue_count];

            /* Unlock while building and sending. */
            pthread_mutex_unlock(&ca->tx_mutex);

            uint8_t buf[CAN_MAX_PAYLOAD];
            size_t len;
            if (build_payload(req.pgn, &ca->sensors, &ca->identity, buf, sizeof(buf), &len) == 0)
                can_send(ca->sock, req.pgn, req.requester_addr, buf, len);

            pthread_mutex_lock(&ca->tx_mutex);
        }

        pthread_mutex_unlock(&ca->tx_mutex);

        /* Periodic sensor PGNs. */
        uint64_t now_ms = get_time_ms();
        for (size_t i = 0; i < pgn_tasks_count; i++) {
            if (now_ms - pgn_tasks[i].last_tx_ms < pgn_tasks[i].tx_rate_ms)
                continue;

            pthread_mutex_lock(&ca->sensors_mutex);
            sensor_values_t sensors = ca->sensors;
            pthread_mutex_unlock(&ca->sensors_mutex);

            uint8_t pbuf[CAN_MAX_PAYLOAD];
            size_t plen;
            if (build_payload(pgn_tasks[i].pgn, &sensors, &ca->identity, pbuf, sizeof(pbuf),
                              &plen) == 0)
                can_send(ca->sock, pgn_tasks[i].pgn, J1939_NO_ADDR, pbuf, plen);

            pgn_tasks[i].last_tx_ms = now_ms;
        }
    }

    printf("[tx] Thread exiting.\n");
    return NULL;
}

/* PUBLIC API */

ca_t* ca_create(const device_name_t* name, uint8_t preferred_addr, const ca_identity_t* identity) {
    ca_t* ca = calloc(1, sizeof *ca);
    if (ca == NULL) {
        perror("ca_create: calloc");
        return NULL;
    }

    ca->name = name->value;
    ca->preferred_addr = preferred_addr;
    ca->identity = *identity;
    ca->sock = -1;

    return ca;
}

/* INTERNAL FUNCTIONS */

void ca_receive(ca_t* ca, uint32_t pgn, uint8_t src_addr, const uint8_t* buf, size_t len,
                int sock) {
    parsed_request_t request = {0};
    if (parse_request(pgn, buf, len, &request) < 0)
        return;

    int new_addr;

    switch (pgn) {
    case PGN_59904:
        /* Another node is requesting a PGN — queue it for TX.
         * If the PGN is unsupported, send a NACK immediately. */
        pthread_mutex_lock(&ca->tx_mutex);
        if (handle_request(request.pgn, src_addr, ca->request_queue, &ca->request_queue_count) <
            0) {
            pthread_mutex_unlock(&ca->tx_mutex);
            uint8_t nack_buf[8];
            size_t nack_len;
            if (build_pgn_59392_payload(src_addr, request.pgn, nack_buf, sizeof(nack_buf),
                                        &nack_len) == 0)
                can_send(sock, PGN_59392, src_addr, nack_buf, nack_len);
        } else {
            pthread_cond_signal(&ca->tx_cond);
            pthread_mutex_unlock(&ca->tx_mutex);
        }
        break;

    case PGN_60928:
        /*
         * Address Claimed — check for contention.
         * If the sender's NAME is lower than ours and they are claiming
         * our address we lost and need to reclaim another address.
         * Cannot Claim Address frames (src_addr == 0xFE) are ignored.
         */
        if (src_addr != J1939_IDLE_ADDR && src_addr == ca->claimed_addr &&
            request.name != ca->name && request.name < ca->name) {
            new_addr = can_address_claim_dynamic(sock, ca->name, ca->claimed_addr + 1);
            if (new_addr < 0) {
                fprintf(stderr, "[ca] Address range exhausted, shutting down.\n");
                ca->running = 0;
                break;
            }
            ca->claimed_addr = (uint8_t)new_addr;
        }
        break;

    case PGN_65240:
        /*
         * Commanded Address — another node is telling us to change address.
         * Only act if the target NAME matches ours.
         */
        if (request.name == ca->name) {
            new_addr = can_address_claim(sock, ca->name, request.new_addr);
            if (new_addr < 0) {
                new_addr = can_address_claim_dynamic(sock, ca->name, ca->preferred_addr);
                if (new_addr < 0) {
                    fprintf(stderr, "[ca] Cannot claim any address, shutting down.\n");
                    ca->running = 0;
                    break;
                }
            }
            ca->claimed_addr = (uint8_t)new_addr;
        }
        break;
    }
}

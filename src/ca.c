/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ca.h"
#include "sensors.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* TASKS */

typedef struct {
    uint32_t pgn;
    uint32_t tx_rate_ms;
    uint64_t last_tx_ms;
} pgn_task_t;

/*
 * Template arrays — read-only, used to initialise each CA instance.
 * The mutable per-CA copies live in ca_t so each CA tracks its own
 * last_tx_ms and last_poll_ms independently.
 */
static const sensor_task_t sensor_tasks_template[] = {
    {.poll_rate_ms = 1000, .read = ambient_temp_read, .write = ambient_temp_write},
};

static const pgn_task_t pgn_tasks_template[] = {
    {.pgn = PGN_65269, .tx_rate_ms = 1000},
};

#define SENSOR_TASKS_COUNT (sizeof(sensor_tasks_template) / sizeof(sensor_tasks_template[0]))
#define PGN_TASKS_COUNT (sizeof(pgn_tasks_template) / sizeof(pgn_tasks_template[0]))

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
    sensor_task_t sensor_tasks[SENSOR_TASKS_COUNT];
    pgn_task_t pgn_tasks[PGN_TASKS_COUNT];
    pthread_t rx_tid;
    pthread_t tx_tid;
    pthread_t sensor_tid;
};

/* HELPERS */

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* SENSORS POLL */

static void sensors_poll(ca_t* ca) {
    uint64_t now_ms = get_time_ms();

    for (size_t i = 0; i < SENSOR_TASKS_COUNT; i++) {
        if (now_ms - ca->sensor_tasks[i].last_poll_ms < ca->sensor_tasks[i].poll_rate_ms)
            continue;

        /* Read from hardware into local buffer — no lock held during hardware access. */
        uint8_t buf[64];
        if (ca->sensor_tasks[i].read(buf) < 0) {
            ca->sensor_tasks[i].last_poll_ms = now_ms;
            continue;
        }

        /* Write into sensor_values_t under the mutex. */
        pthread_mutex_lock(&ca->sensors_mutex);
        ca->sensor_tasks[i].write(&ca->sensors, buf);
        pthread_mutex_unlock(&ca->sensors_mutex);

        ca->sensor_tasks[i].last_poll_ms = now_ms;
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

static void* rx_thread(void* arg) {
    ca_t* ca = (ca_t*)arg;

    printf("[rx] Thread started. addr=0x%02X tid=%lu\n", ca->claimed_addr, pthread_self());

    while (ca->running) {
        uint8_t buf[CAN_MAX_PAYLOAD];
        size_t len;
        uint32_t pgn;
        uint8_t src_addr;

        int rc = can_receive(ca->sock, &pgn, &src_addr, buf, sizeof(buf), &len);

        if (rc < 0)
            break;

        if (rc > 0)
            continue;

        ca_receive(ca, pgn, src_addr, buf, len);
    }

    printf("[rx] Thread exiting. addr=0x%02X\n", ca->claimed_addr);
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
        for (size_t i = 0; i < PGN_TASKS_COUNT; i++) {
            if (now_ms - ca->pgn_tasks[i].last_tx_ms < ca->pgn_tasks[i].tx_rate_ms)
                continue;

            pthread_mutex_lock(&ca->sensors_mutex);
            sensor_values_t sensors = ca->sensors;
            pthread_mutex_unlock(&ca->sensors_mutex);

            uint8_t pbuf[CAN_MAX_PAYLOAD];
            size_t plen;
            if (build_payload(ca->pgn_tasks[i].pgn, &sensors, &ca->identity, pbuf, sizeof(pbuf),
                              &plen) == 0)
                can_send(ca->sock, ca->pgn_tasks[i].pgn, J1939_NO_ADDR, pbuf, plen);

            ca->pgn_tasks[i].last_tx_ms = now_ms;
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

    memcpy(ca->sensor_tasks, sensor_tasks_template, sizeof(sensor_tasks_template));
    memcpy(ca->pgn_tasks, pgn_tasks_template, sizeof(pgn_tasks_template));

    return ca;
}

void ca_destroy(ca_t* ca) {
    pthread_mutex_destroy(&ca->tx_mutex);
    pthread_cond_destroy(&ca->tx_cond);
    pthread_mutex_destroy(&ca->sensors_mutex);
    free(ca);
}

/* INTERNAL FUNCTIONS */

uint8_t ca_get_claimed_addr(const ca_t* ca) {
    return ca->claimed_addr;
}

int ca_start(ca_t* ca, const char* ifname) {
    ca->sock = can_socket_create(ifname);
    if (ca->sock < 0) {
        fprintf(stderr, "ca_start: failed to create socket on %s\n", ifname);
        return -1;
    }

    int addr = can_address_claim_dynamic(ca->sock, ca->name, ca->preferred_addr);
    if (addr < 0) {
        fprintf(stderr, "ca_start: failed to claim address\n");
        close(ca->sock);
        ca->sock = -1;
        return -1;
    }
    ca->claimed_addr = (uint8_t)addr;

    printf("CA claimed address 0x%02X\n", ca->claimed_addr);

    if (pthread_mutex_init(&ca->tx_mutex, NULL) != 0 ||
        pthread_cond_init(&ca->tx_cond, NULL) != 0 ||
        pthread_mutex_init(&ca->sensors_mutex, NULL) != 0) {
        perror("ca_start: pthread init failed");
        close(ca->sock);
        ca->sock = -1;
        return -1;
    }

    ca->running = 1;

    /* sensor first — TX depends on sensor values being available.
     * RX last — it calls ca_receive() which may signal TX. */
    if (pthread_create(&ca->sensor_tid, NULL, sensor_thread, ca) != 0 ||
        pthread_create(&ca->tx_tid, NULL, tx_thread, ca) != 0 ||
        pthread_create(&ca->rx_tid, NULL, rx_thread, ca) != 0) {
        perror("ca_start: pthread_create failed");
        ca->running = 0;
        close(ca->sock);
        ca->sock = -1;
        return -1;
    }

    return 0;
}

void ca_stop(ca_t* ca) {
    ca->running = 0;
    pthread_cond_signal(&ca->tx_cond);

    /* Closing the socket unblocks can_receive() in the RX thread. */
    if (ca->sock >= 0) {
        close(ca->sock);
        ca->sock = -1;
    }

    pthread_join(ca->rx_tid, NULL);
    pthread_join(ca->sensor_tid, NULL);
    pthread_join(ca->tx_tid, NULL);
    printf("CA 0x%02X stopped\n", ca->claimed_addr);
}

void ca_receive(ca_t* ca, uint32_t pgn, uint8_t src_addr, const uint8_t* buf, size_t len) {
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
                can_send(ca->sock, PGN_59392, src_addr, nack_buf, nack_len);
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
            new_addr = can_address_claim_dynamic(ca->sock, ca->name, ca->claimed_addr + 1);
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
            new_addr = can_address_claim(ca->sock, ca->name, request.new_addr);
            if (new_addr < 0) {
                new_addr = can_address_claim_dynamic(ca->sock, ca->name, ca->preferred_addr);
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

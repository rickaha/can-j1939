/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ecu.h"
#include "pgn_data.h"
#include "sensors.h"
#include "stack_utils.h"
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* STRUCTS */

typedef struct {
    int sock;
    uint8_t claimed_addr;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pgn_request_t request_queue[REQUEST_QUEUE_SIZE];
    uint8_t request_queue_count;
} rxtx_ctx_t;

typedef struct {
    volatile sig_atomic_t running;
    rxtx_ctx_t rxtx;
    sensor_values_t sensors;
    pthread_mutex_t sensors_mutex;
    pthread_t sensor_tid;
    pthread_t rx_tid;
    pthread_t tx_tid;
    uint64_t name;
    uint8_t preferred_addr;
} ecu_ctx_t;

/* SINGLETON */

static ecu_ctx_t ecu = {
    .rxtx.sock = -1,
};

/* SENSORS POLL */

static void sensors_poll(ecu_ctx_t* ctx) {
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
        pthread_mutex_lock(&ctx->sensors_mutex);
        sensor_tasks[i].write(&ctx->sensors, buf);
        pthread_mutex_unlock(&ctx->sensors_mutex);

        sensor_tasks[i].last_poll_ms = now_ms;
    }
}

/* THREADS */

static void* rx_thread(void* arg) {
    ecu_ctx_t* ctx = (ecu_ctx_t*)arg;

    printf("[rx] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        uint8_t buf[CAN_MAX_PAYLOAD];
        size_t len;
        uint32_t pgn;
        uint8_t src_addr;
        int new_addr;

        /*  if returns -1 we break out of the loop, which exits the thread. That handles socket
         *  closure cleanly.
         */
        if (can_receive(ctx->rxtx.sock, &pgn, &src_addr, buf, sizeof(buf), &len) < 0)
            break;

        parsed_request_t request = {0};
        if (parse_request(pgn, buf, len, &request) < 0)
            continue;

        switch (pgn) {
        case PGN_59904:
            /* Another node is requesting a PGN — queue it for TX.
             * If the PGN is unsupported, send a NACK immediately. */
            pthread_mutex_lock(&ctx->rxtx.mutex);
            if (handle_request(request.pgn, src_addr, ctx->rxtx.request_queue,
                               &ctx->rxtx.request_queue_count) < 0) {
                pthread_mutex_unlock(&ctx->rxtx.mutex);
                uint8_t nack_buf[8];
                size_t nack_len;
                if (build_pgn_59392_payload(src_addr, request.pgn, nack_buf, sizeof(nack_buf),
                                            &nack_len) == 0)
                    can_send(ctx->rxtx.sock, PGN_59392, src_addr, nack_buf, nack_len);
            } else {
                pthread_cond_signal(&ctx->rxtx.cond);
                pthread_mutex_unlock(&ctx->rxtx.mutex);
            }
            break;

        case PGN_60928:
            /*
             * Address Claimed — check for contention.
             * If the sender's NAME is lower than ours and they are claiming
             * our address we lost and need to reclaim another address.
             * Cannot Claim Address frames (src_addr == 0xFE) are ignored.
             */
            if (src_addr != J1939_IDLE_ADDR && src_addr == ctx->rxtx.claimed_addr &&
                request.name != ctx->name && request.name < ctx->name) {
                new_addr = can_address_claim_dynamic(ctx->rxtx.sock, ctx->name,
                                                     ctx->rxtx.claimed_addr + 1);
                if (new_addr < 0) {
                    fprintf(stderr, "[rx] Address range exhausted, shutting down.\n");
                    ctx->running = 0;
                    break;
                }
                ctx->rxtx.claimed_addr = (uint8_t)new_addr;
            }
            break;

        case PGN_65240:
            /*
             * Commanded Address — another node is telling us to change address.
             * Only act if the target NAME matches ours.
             */
            if (request.name == ctx->name) {
                new_addr = can_address_claim(ctx->rxtx.sock, ctx->name, request.new_addr);
                if (new_addr < 0) {
                    new_addr =
                        can_address_claim_dynamic(ctx->rxtx.sock, ctx->name, ctx->preferred_addr);
                    if (new_addr < 0) {
                        fprintf(stderr, "[rx] Cannot claim any address, shutting down.\n");
                        ctx->running = 0;
                        break;
                    }
                }
                ctx->rxtx.claimed_addr = (uint8_t)new_addr;
            }
            break;
        }
    }

    printf("[rx] Thread exiting.\n");
    return NULL;
}

static void* tx_thread(void* arg) {
    ecu_ctx_t* ctx = (ecu_ctx_t*)arg;

    printf("[tx] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        // Wait for RX signal or wake up every 10ms to service periodic PGNs.
        pthread_mutex_lock(&ctx->rxtx.mutex);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 10000000L; /* 10ms */
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&ctx->rxtx.cond, &ctx->rxtx.mutex, &deadline);

        // Drain on-request queue
        while (ctx->rxtx.request_queue_count > 0) {
            ctx->rxtx.request_queue_count--;
            pgn_request_t req = ctx->rxtx.request_queue[ctx->rxtx.request_queue_count];

            // Unlock while building and sending
            pthread_mutex_unlock(&ctx->rxtx.mutex);

            uint8_t buf[CAN_MAX_PAYLOAD];
            size_t len;
            if (build_payload(req.pgn, &ctx->sensors, buf, sizeof(buf), &len) == 0)
                can_send(ctx->rxtx.sock, req.pgn, req.requester_addr, buf, len);

            pthread_mutex_lock(&ctx->rxtx.mutex);
        }

        pthread_mutex_unlock(&ctx->rxtx.mutex);

        // Periodic sensor PGNs
        uint64_t now_ms = get_time_ms();
        for (size_t i = 0; i < pgn_tasks_count; i++) {
            if (now_ms - pgn_tasks[i].last_tx_ms < pgn_tasks[i].tx_rate_ms)
                continue;

            pthread_mutex_lock(&ctx->sensors_mutex);
            sensor_values_t sensors = ctx->sensors;
            pthread_mutex_unlock(&ctx->sensors_mutex);

            uint8_t pbuf[CAN_MAX_PAYLOAD];
            size_t plen;
            if (build_payload(pgn_tasks[i].pgn, &sensors, pbuf, sizeof(pbuf), &plen) == 0)
                can_send(ctx->rxtx.sock, pgn_tasks[i].pgn, J1939_NO_ADDR, pbuf, plen);

            pgn_tasks[i].last_tx_ms = now_ms;
        }
    }

    printf("[tx] Thread exiting.\n");
    return NULL;
}

static void* sensor_thread(void* arg) {
    ecu_ctx_t* ctx = (ecu_ctx_t*)arg;

    printf("[sensor] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        sensors_poll(ctx);
    }

    printf("[sensor] Thread exiting.\n");
    return NULL;
}

/* PUBLIC API */

void ecu_set_identity(const component_id_t* component_id, const software_id_t* software_id,
                      const ecu_id_t* ecu_id) {
    pgn_data_init(component_id, software_id, ecu_id);
}

void ecu_set_address_config(uint64_t name, uint8_t preferred_addr) {
    ecu.name = name;
    ecu.preferred_addr = preferred_addr;
}

int ecu_connect(const char* interface) {
    ecu.rxtx.sock = can_socket_create(interface);
    if (ecu.rxtx.sock < 0) {
        fprintf(stderr, "ecu_connect: failed to create socket on %s\n", interface);
        return -1;
    }
    return 0;
}

int ecu_start(void) {
    int addr = can_address_claim_dynamic(ecu.rxtx.sock, ecu.name, ecu.preferred_addr);
    if (addr < 0) {
        fprintf(stderr, "ecu_start: failed to claim address\n");
        return -1;
    }
    ecu.rxtx.claimed_addr = (uint8_t)addr;

    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&ecu.rxtx.mutex, NULL) != 0 ||
        pthread_cond_init(&ecu.rxtx.cond, NULL) != 0 ||
        pthread_mutex_init(&ecu.sensors_mutex, NULL) != 0) {
        perror("ecu_start: pthread init failed");
        return -1;
    }

    ecu.running = 1;

    printf("Successfully claimed address 0x%02X. Entering main loop...\n", ecu.rxtx.claimed_addr);

    /* Create threads in dependency order:
     * sensor first — TX depends on sensor values being available.
     * RX before TX  — RX must be listening before TX starts sending. */
    if (pthread_create(&ecu.sensor_tid, NULL, sensor_thread, &ecu) != 0 ||
        pthread_create(&ecu.rx_tid, NULL, rx_thread, &ecu) != 0 ||
        pthread_create(&ecu.tx_tid, NULL, tx_thread, &ecu) != 0) {
        perror("ecu_start: pthread_create failed");
        ecu.running = 0;
        return -1;
    }

    /*  Wait for all threads to finish (exit when they see running == 0) */
    pthread_join(ecu.sensor_tid, NULL);
    pthread_join(ecu.rx_tid, NULL);
    pthread_join(ecu.tx_tid, NULL);

    return 0;
}

void ecu_stop(void) {
    ecu.running = 0;
}

void ecu_disconnect(void) {
    if (ecu.rxtx.sock >= 0) {
        close(ecu.rxtx.sock);
        ecu.rxtx.sock = -1;
    }
    pthread_mutex_destroy(&ecu.rxtx.mutex);
    pthread_cond_destroy(&ecu.rxtx.cond);
    pthread_mutex_destroy(&ecu.sensors_mutex);
}

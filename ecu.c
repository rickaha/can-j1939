/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "pgn_data.h"
#include "sensors.h"
#include "stack_utils.h"
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PREFERRED_ADDRESS 0x80
#define CAN_INTERFACE "vcan0"

/* NODE IDENTITY */

static const device_name_t ECU_NAME = {
    .identity_number = 0x003039, // Decimal 12345
    .mfg_code = 0x3FF,           // Reserved/Non-specific
    .function_inst = 0x00,       // First instance
    .function = 0x19,            // 25 = Peripheral Device
    .reserved = 0x0,             // Must be 0
    .vehicle_system = 0x00,      // Non-specific
    .system_inst = 0x00,         // First instance
    .industry_group = 0x05,      // 5 = Industrial/Process Control
    .arbitrary_addr = 0x01       // Enable Dynamic Address Claiming
};

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
} node_ctx_t;

/* Tasks */

static sensor_task_t sensor_tasks[] = {
    /* TODO: add sensor tasks here */
    //  { .poll_rate_ms = 1000, .read = sensor_temperature_read, .value = &ctx.sensors.temperature,
    //  .mutex = &ctx.sensors_mutex},
};

static const size_t sensor_tasks_count = sizeof(sensor_tasks) / sizeof(sensor_tasks[0]);

/* SENSORS POLL */

static void sensors_poll() {
    /* TODO: get monotonic timestamp (clock_gettime CLOCK_MONOTONIC).
     * For each task in sensor_tasks:
     *   - check if poll interval has elapsed since last_poll_ms
     *   - call task.read(task.value) into a local variable — no lock held
     *   - lock task.mutex
     *   - write local variable to task.value
     *   - unlock task.mutex
     *   - update task.last_poll_ms */
    (void)sensor_tasks;
    (void)sensor_tasks_count;
}

/* THREADS */

static void* rx_thread(void* arg) {
    node_ctx_t* ctx = (node_ctx_t*)arg;

    printf("[rx] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        uint8_t buf[CAN_MAX_PAYLOAD];
        size_t len;
        uint32_t pgn;
        uint8_t src_addr;
        int new_addr;

        //  if returns -1 we break out of the loop, which exits the thread. That handles socket
        //  closure cleanly.
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
                request.name != ECU_NAME.value && request.name < ECU_NAME.value) {
                new_addr = can_address_claim_dynamic(ctx->rxtx.sock, ECU_NAME.value,
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
            if (request.name == ECU_NAME.value) {
                new_addr = can_address_claim(ctx->rxtx.sock, ECU_NAME.value, request.new_addr);
                if (new_addr < 0) {
                    new_addr = can_address_claim_dynamic(ctx->rxtx.sock, ECU_NAME.value,
                                                         PREFERRED_ADDRESS);
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
    node_ctx_t* ctx = (node_ctx_t*)arg;

    printf("[tx] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        // Wait for RX signal or periodic timeout
        pthread_mutex_lock(&ctx->rxtx.mutex);
        pthread_cond_wait(&ctx->rxtx.cond, &ctx->rxtx.mutex);

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

        /* --- Periodic sensor PGNs ---------------------------------------- */
        /* For each periodic PGN:
         *   - lock sensors mutex
         *   - read sensor value into local variable
         *   - unlock sensors mutex
         *   - build_payload(pgn, &ctx->sensors, buf, sizeof(buf), &len)
         *   - can_send() */
    }

    printf("[tx] Thread exiting.\n");
    return NULL;
}

static void* sensor_thread(void* arg) {
    node_ctx_t* ctx = (node_ctx_t*)arg;

    printf("[sensor] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        sensors_poll();
    }

    printf("[sensor] Thread exiting.\n");
    return NULL;
}

int main() {
    printf("Starting J1939 Sensor Hub...\n");

    node_ctx_t ctx = {0};
    ctx.running = 1;

    /* Initialise pgn_data with device-specific identity. */
    static const component_id_t COMPONENT_ID = {
        .make = "RPi",
        .model = "Sensor-Hub",
        .serial = "SN-00001",
        .unit = "U-01",
    };
    pgn_data_init(&COMPONENT_ID);

    // Create socket and claim address dynamically.
    ctx.rxtx.sock = can_socket_create(CAN_INTERFACE);
    if (ctx.rxtx.sock < 0) {
        fprintf(stderr, "Failed to create J1939 socket on %s. Is the interface up?\n",
                CAN_INTERFACE);
        return EXIT_FAILURE;
    }
    int addr = can_address_claim_dynamic(ctx.rxtx.sock, ECU_NAME.value, PREFERRED_ADDRESS);
    if (addr < 0) {
        fprintf(stderr, "Failed to claim a J1939 address on %s.\n", CAN_INTERFACE);
        close(ctx.rxtx.sock);
        return EXIT_FAILURE;
    }
    ctx.rxtx.claimed_addr = (uint8_t)addr;

    // Initialize synchronization primitives
    if (pthread_mutex_init(&ctx.rxtx.mutex, NULL) != 0 ||
        pthread_cond_init(&ctx.rxtx.cond, NULL) != 0 ||
        pthread_mutex_init(&ctx.sensors_mutex, NULL) != 0) {
        perror("pthread mutex/cond init failed");
        close(ctx.rxtx.sock);
        return EXIT_FAILURE;
    }

    printf("Successfully claimed address 0x%02X. Entering main loop...\n", PREFERRED_ADDRESS);

    // Create threads in dependency order:
    // sensor first — TX depends on sensor values being available.
    // RX before TX  — RX must be listening before TX starts sending.
    pthread_t rx_tid, tx_tid, sensor_tid;
    if (pthread_create(&sensor_tid, NULL, sensor_thread, &ctx) != 0 ||
        pthread_create(&rx_tid, NULL, rx_thread, &ctx) != 0 ||
        pthread_create(&tx_tid, NULL, tx_thread, &ctx) != 0) {
        perror("pthread_create failed");
        close(ctx.rxtx.sock);
        pthread_mutex_destroy(&ctx.rxtx.mutex);
        pthread_cond_destroy(&ctx.rxtx.cond);
        pthread_mutex_destroy(&ctx.sensors_mutex);
        return EXIT_FAILURE;
    }

    // Wait for all threads to finish (exit when they see running == 0)
    pthread_join(sensor_tid, NULL);
    pthread_join(rx_tid, NULL);
    pthread_join(tx_tid, NULL);

    // Cleanup
    pthread_mutex_destroy(&ctx.rxtx.mutex);
    pthread_cond_destroy(&ctx.rxtx.cond);
    pthread_mutex_destroy(&ctx.sensors_mutex);
    close(ctx.rxtx.sock);

    printf("Sensor Hub shut down cleanly.\n");
    return 0;
}

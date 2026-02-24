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

static const component_id_t COMPONENT_ID = {
    .make = "RPi",
    .model = "Sensor-Hub",
    .serial = "SN-00001",
    .unit = "U-01",
};

/* STRUCTS */

typedef struct {
    int sock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pgn_request_t request_queue[REQUEST_QUEUE_SIZE];
    uint8_t request_queue_count;
} rxtx_ctx_t;

typedef struct {
    pthread_mutex_t mutex;
    /* Sensor values go here as they are implemented*/
} sensor_values_t;

typedef struct {
    volatile sig_atomic_t running;
    rxtx_ctx_t rxtx;
    sensor_values_t sensors;
} node_ctx_t;

/* Tasks */

static sensor_task_t sensor_tasks[] = {
    /* TODO: add sensor tasks here */
    //  { .poll_rate_ms = 1000, .read = sensor_temperature_read, .value = &ctx.sensors.temperature},
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

        //  if returns -1 we break out of the loop, which exits the thread. That handles socket
        //  closure cleanly.
        if (can_receive(ctx->rxtx.sock, &pgn, &src_addr, buf, sizeof(buf), &len) < 0)
            break;

        // anything that isn't a request PGN gets silently dropped
        if (pgn != PGN_59904)
            continue;

        // a malformed payload doesn't kill the thread, it just gets discarded.
        uint32_t requested_pgn;
        if (parse_pgn_59904_payload(buf, len, &requested_pgn) < 0)
            continue;

        pthread_mutex_lock(&ctx->rxtx.mutex);
        handle_request(requested_pgn, src_addr, ctx->rxtx.request_queue,
                       &ctx->rxtx.request_queue_count);
        pthread_cond_signal(&ctx->rxtx.cond);
        pthread_mutex_unlock(&ctx->rxtx.mutex);
    }

    printf("[rx] Thread exiting.\n");
    return NULL;
}

static void* tx_thread(void* arg) {
    node_ctx_t* ctx = (node_ctx_t*)arg;

    /* TODO: implement TX thread
     * - wake on cond signal or transmit period timeout
     * - drain request queue and send on-request PGNs
     * - build and send periodic sensor PGNs */

    (void)ctx;
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

    // Open J1939 socket
    ctx.rxtx.sock = can_open_socket(CAN_INTERFACE, ECU_NAME.value, PREFERRED_ADDRESS);
    if (ctx.rxtx.sock < 0) {
        fprintf(stderr, "Failed to initialize J1939 on %s. Is the interface up?\n", CAN_INTERFACE);
        return EXIT_FAILURE;
    }

    // Initialize synchronization primitives
    if (pthread_mutex_init(&ctx.rxtx.mutex, NULL) != 0 ||
        pthread_cond_init(&ctx.rxtx.cond, NULL) != 0 ||
        pthread_mutex_init(&ctx.sensors.mutex, NULL) != 0) {
        perror("pthread mutex/cond init failed");
        close(ctx.rxtx.sock);
        return EXIT_FAILURE;
    }

    printf("Successfully claimed address 0x%02X. Entering main loop...\n", PREFERRED_ADDRESS);

    /* Create threads in dependency order:
     * sensor first — TX depends on sensor values being available.
     * RX before TX  — RX must be listening before TX starts sending. */
    pthread_t rx_tid, tx_tid, sensor_tid;
    if (pthread_create(&sensor_tid, NULL, sensor_thread, &ctx) != 0 ||
        pthread_create(&rx_tid, NULL, rx_thread, &ctx) != 0 ||
        pthread_create(&tx_tid, NULL, tx_thread, &ctx) != 0) {
        perror("pthread_create failed");
        close(ctx.rxtx.sock);
        pthread_mutex_destroy(&ctx.rxtx.mutex);
        pthread_cond_destroy(&ctx.rxtx.cond);
        pthread_mutex_destroy(&ctx.sensors.mutex);
        return EXIT_FAILURE;
    }

    // Wait for all threads to finish (exit when they see running == 0)
    pthread_join(sensor_tid, NULL);
    pthread_join(rx_tid, NULL);
    pthread_join(tx_tid, NULL);

    // Cleanup
    pthread_mutex_destroy(&ctx.rxtx.mutex);
    pthread_cond_destroy(&ctx.rxtx.cond);
    pthread_mutex_destroy(&ctx.sensors.mutex);
    close(ctx.rxtx.sock);

    printf("Sensor Hub shut down cleanly.\n");
    return 0;
}

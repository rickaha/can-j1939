/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "pgn_data.h"
#include "stack_utils.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PREFERRED_ADDRESS 0x80
#define TRANSMIT_RATE_MS 100
#define CAN_INTERFACE "vcan0"

/* NODE IDENTITY */

static const j1939_name_t ECU_NAME = {
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

static const j1939_component_id_t COMPONENT_ID = {
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

/* THREADS */

static void* rx_thread(void* arg) {
    node_ctx_t* ctx = (node_ctx_t*)arg;

    /* TODO: implement RX thread
     * - block on recvfrom()
     * - parse incoming PGN 59904 requests
     * - call handle_request() under mutex
     * - signal cond to wake TX thread */

    (void)ctx;
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

    /* TODO: implement sensor thread
     * - poll sensors at their respective rates
     * - update sensor_data under mutex */

    (void)ctx;
    return NULL;
}

int main() {

    printf("Starting J1939 Sensor Hub...\n");

    int sock = j1939_socket_open(CAN_INTERFACE, ECU_NAME.value, PREFERRED_ADDRESS);

    if (sock < 0) {
        fprintf(stderr, "Failed to initialize J1939 on %s. Is the interface up?\n", CAN_INTERFACE);
        return EXIT_FAILURE;
    }

    printf("Successfully claimed address 0x%02X. Entering main loop...\n", PREFERRED_ADDRESS);

    while (1) {
        // Sensor logic goes here
        printf("Broadcasting PGNs to the bus...\n");

        usleep(TRANSMIT_RATE_MS * 1000);
    }

    close(sock);
    return 0;
}

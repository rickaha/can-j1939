/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ecu.h"
#include "ca.h"
#include "pgn_data.h"
#include "stack_utils.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define MAX_CA 8

/* STRUCTS */

typedef struct {
    volatile int running;
    int sock;
    pthread_t rx_tid;
    ca_t* ca_list[MAX_CA];
    size_t ca_count;
} ecu_ctx_t;

/* SINGLETON */

static ecu_ctx_t ecu = {
    .sock = -1,
};

/* THREADS */

static void* rx_thread(void* arg) {
    ecu_ctx_t* ctx = (ecu_ctx_t*)arg;

    printf("[rx] Thread started. tid=%lu\n", pthread_self());

    while (ctx->running) {
        uint8_t buf[CAN_MAX_PAYLOAD];
        size_t len;
        uint32_t pgn;
        uint8_t src_addr;

        /* If returns -1 we break out of the loop, which exits the thread.
         * That handles socket closure cleanly. */
        if (can_receive(ctx->sock, &pgn, &src_addr, buf, sizeof(buf), &len) < 0)
            break;

        /* Dispatch to the CA that owns the destination address. */
        for (size_t i = 0; i < ctx->ca_count; i++) {
            ca_receive(ctx->ca_list[i], pgn, src_addr, buf, len, ctx->sock);
        }
    }

    printf("[rx] Thread exiting.\n");
    return NULL;
}

/* PUBLIC API */

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

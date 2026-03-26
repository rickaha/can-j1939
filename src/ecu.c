/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ecu.h"
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

        int rc = can_receive(ctx->sock, &pgn, &src_addr, buf, sizeof(buf), &len);

        if (rc < 0)
            break;

        if (rc > 0)
            continue;

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
    ecu.sock = can_socket_create(interface);
    if (ecu.sock < 0) {
        fprintf(stderr, "ecu_connect: failed to create socket on %s\n", interface);
        return -1;
    }

    ecu.running = 1;

    if (pthread_create(&ecu.rx_tid, NULL, rx_thread, &ecu) != 0) {
        perror("ecu_connect: pthread_create failed");
        close(ecu.sock);
        ecu.sock = -1;
        ecu.running = 0;
        return -1;
    }

    return 0;
}

int ecu_start_ca(ca_t* ca) {
    if (ecu.ca_count >= MAX_CA) {
        fprintf(stderr, "ecu_start_ca: max CA count reached\n");
        return -1;
    }

    if (ca_start(ca, ecu.sock) < 0) {
        fprintf(stderr, "ecu_start_ca: ca_start failed\n");
        return -1;
    }

    ecu.ca_list[ecu.ca_count++] = ca;
    return 0;
}

void ecu_stop_ca(ca_t* ca) {
    ca_stop(ca);

    /* Remove from CA list. */
    for (size_t i = 0; i < ecu.ca_count; i++) {
        if (ecu.ca_list[i] == ca) {
            ecu.ca_list[i] = ecu.ca_list[--ecu.ca_count];
            ecu.ca_list[ecu.ca_count] = NULL;
            break;
        }
    }
}

void ecu_disconnect(void) {
    ecu.running = 0;

    if (ecu.sock >= 0) {
        close(ecu.sock);
        ecu.sock = -1;
    }

    pthread_join(ecu.rx_tid, NULL);
}

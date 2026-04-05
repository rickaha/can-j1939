/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ecu.h"
#include "ca.h"
#include <stdio.h>
#include <string.h>

#define MAX_CA 8

/* STRUCTS */

typedef struct {
    volatile int running;
    char ifname[32];
    ca_t* ca_list[MAX_CA];
    size_t ca_count;
} ecu_ctx_t;

/* SINGLETON */

static ecu_ctx_t ecu;

/* PUBLIC API */

int ecu_connect(const char* interface) {
    strncpy(ecu.ifname, interface, sizeof(ecu.ifname) - 1);
    ecu.ifname[sizeof(ecu.ifname) - 1] = '\0';
    ecu.running = 1;
    return 0;
}

int ecu_start_ca(ca_t* ca) {
    if (ecu.ca_count >= MAX_CA) {
        fprintf(stderr, "ecu_start_ca: max CA count reached\n");
        return -1;
    }

    if (ca_start(ca, ecu.ifname) < 0) {
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

    for (size_t i = 0; i < ecu.ca_count; i++) {
        ca_stop(ecu.ca_list[i]);
        ecu.ca_list[i] = NULL;
    }
    ecu.ca_count = 0;
}

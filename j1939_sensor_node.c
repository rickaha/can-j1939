/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "j1939_stack_utils.h"

#define PREFERRED_ADDRESS 0x80
#define TRANSMIT_RATE_MS 100
#define CAN_INTERFACE "vcan0"

int main() {
    // Start with a simple 64-bit value.
    uint64_t my_name = 0x123456789ABCDEF0ULL;

    printf("Starting J1939 Sensor Hub...\n");

    int sock = j1939_socket_open(CAN_INTERFACE, my_name, PREFERRED_ADDRESS);

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

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


static const j1939_name_t ECU_NAME = {
    .identity_number = 0x003039, // Decimal 12345
    .mfg_code        = 0x3FF,    // Reserved/Non-specific
    .function_inst   = 0x00,     // First instance
    .function        = 0x19,     // 25 = Peripheral Device
    .reserved        = 0x0,      // Must be 0
    .vehicle_system  = 0x00,     // Non-specific
    .system_inst     = 0x00,     // First instance
    .industry_group  = 0x05,     // 5 = Industrial/Process Control
    .arbitrary_addr  = 0x01      // Enable Dynamic Address Claiming
};

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

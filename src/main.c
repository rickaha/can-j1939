/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ca.h"
#include "ecu.h"
#include "pgn_data.h"
#include "stack_utils.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PREFERRED_ADDRESS 0x80
#define CAN_INTERFACE "vcan0"

/* NODE IDENTITY */

static const device_name_t ECU_NAME = {
    .identity_number = 0x003039, /* Decimal 12345                    */
    .mfg_code = 0x3FF,           /* Reserved/Non-specific            */
    .function_inst = 0x00,       /* First instance                   */
    .function = 0x19,            /* 25 = Peripheral Device           */
    .reserved = 0x0,             /* Must be 0                        */
    .vehicle_system = 0x00,      /* Non-specific                     */
    .system_inst = 0x00,         /* First instance                   */
    .industry_group = 0x05,      /* 5 = Industrial/Process Control   */
    .arbitrary_addr = 0x01       /* Enable Dynamic Address Claiming  */
};

static const ca_identity_t CA_IDENTITY = {
    .component_id = {.make = "RPi", .model = "Sensor-Hub", .serial = "SN-00001", .unit = "U-01"},
    .software_id = {.version = "1.0.0"},
    .ecu_id = {.part_number = "PN-00001",
               .serial = "SN-00001",
               .location = "Main-Board",
               .type = "Sensor-Hub"},
};

/* SIGNAL HANDLER */

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    printf("Starting J1939 Sensor Hub...\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ecu_connect(CAN_INTERFACE) < 0) {
        fprintf(stderr, "Failed to connect to %s. Is the interface up?\n", CAN_INTERFACE);
        return EXIT_FAILURE;
    }

    ca_t* ca = ca_create(&ECU_NAME, PREFERRED_ADDRESS, &CA_IDENTITY);
    if (ca == NULL) {
        ecu_disconnect();
        return EXIT_FAILURE;
    }

    if (ecu_start_ca(ca) < 0) {
        ca_destroy(ca);
        ecu_disconnect();
        return EXIT_FAILURE;
    }

    while (running)
        pause();

    printf("\nShutting down...\n");

    ecu_stop_ca(ca);
    ca_destroy(ca);
    ecu_disconnect();

    printf("Sensor Hub shut down cleanly.\n");
    return EXIT_SUCCESS;
}

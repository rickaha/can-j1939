/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "ecu.h"
#include "pgn_data.h"
#include "stack_utils.h"
#include <stdio.h>
#include <stdlib.h>

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

static const component_id_t COMPONENT_ID = {
    .make = "RPi",
    .model = "Sensor-Hub",
    .serial = "SN-00001",
    .unit = "U-01",
};

static const software_id_t SOFTWARE_ID = {
    .version = "1.0.0",
};

static const ecu_id_t ECU_ID = {
    .part_number = "PN-00001",
    .serial = "SN-00001",
    .location = "Main-Board",
    .type = "Sensor-Hub",
};

int main(void) {
    printf("Starting J1939 Sensor Hub...\n");

    ecu_set_identity(&COMPONENT_ID, &SOFTWARE_ID, &ECU_ID);
    ecu_set_address_config(ECU_NAME.value, PREFERRED_ADDRESS);

    if (ecu_connect(CAN_INTERFACE) < 0) {
        fprintf(stderr, "Failed to connect to %s. Is the interface up?\n", CAN_INTERFACE);
        return EXIT_FAILURE;
    }

    ecu_start(); /* blocks until shutdown */

    ecu_disconnect();

    printf("Sensor Hub shut down cleanly.\n");
    return EXIT_SUCCESS;
}

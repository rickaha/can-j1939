/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef ECU_H
#define ECU_H

#include "pgn_data.h"
#include <stdint.h>

/**
 * Set the device identity strings used in PGN responses.
 * Must be called before ecu_start().
 *
 * @component_id  Component identification (PGN 65259).
 * @software_id   Software identification (PGN 65242).
 * @ecu_id        ECU identification (PGN 64965).
 */
void ecu_set_identity(const component_id_t* component_id, const software_id_t* software_id,
                      const ecu_id_t* ecu_id);

/**
 * Set the J1939 NAME and preferred address for dynamic address claiming.
 * Must be called before ecu_start().
 *
 * @name           64-bit J1939 NAME.
 * @preferred_addr Preferred starting address for dynamic claiming (0x80-0xF7).
 */
void ecu_set_address_config(uint64_t name, uint8_t preferred_addr);

/**
 * Open the CAN socket and bind to @interface.
 * Must be called before ecu_start().
 *
 * @interface  CAN interface name (e.g. "vcan0").
 *
 * Returns 0 on success, -1 on failure.
 */
int ecu_connect(const char* interface);

/**
 * Claim an address and start all threads.
 * ecu_connect(), ecu_set_identity() and ecu_set_address_config() must
 * have been called first.
 *
 * Blocks until ecu_stop() is called or a thread exits.
 *
 * Returns 0 on success, -1 on failure.
 */
int ecu_start(void);

/**
 * Signal all threads to stop and wait for them to exit.
 */
void ecu_stop(void);

/**
 * Close the CAN socket and free all resources.
 */
void ecu_disconnect(void);

#endif /* ECU_H */

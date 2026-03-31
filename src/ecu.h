/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef ECU_H
#define ECU_H

#include "ca.h"

/**
 * Store the CAN interface name for use by ecu_start_ca().
 * Must be called before ecu_start_ca().
 *
 * @interface  CAN interface name (e.g. "vcan0").
 *
 * Returns 0 on success.
 */
int ecu_connect(const char* interface);

/**
 * Start a CA — creates its socket, claims an address, and starts
 * its threads. Registers the CA in the ECU list for ecu_disconnect().
 * Must be called after ecu_connect().
 *
 * Returns 0 on success, -1 on failure.
 */
int ecu_start_ca(ca_t* ca);

/**
 * Signal all threads to stop and wait for them to exit.
 */
void ecu_stop_ca(ca_t* ca);

/**
 * Close the CAN socket and free all resources.
 */
void ecu_disconnect(void);

#endif /* ECU_H */

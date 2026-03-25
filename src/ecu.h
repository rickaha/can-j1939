/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef ECU_H
#define ECU_H

#include "ca.h"

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

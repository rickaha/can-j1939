/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef CA_H
#define CA_H

#include "pgn_data.h"
#include "stack_utils.h"
#include <stdint.h>

/* Opaque CA handle. */
typedef struct ca_t ca_t;

/**
 * Allocate and initialise a Controller Application instance.
 *
 * @name           64-bit J1939 NAME — must be unique per CA on the bus.
 * @preferred_addr Preferred starting address for dynamic claiming (0x80-0xF7).
 * @identity       Device identity used in PGN responses.
 *
 * Returns a pointer to the new CA, or NULL on failure.
 */
ca_t* ca_create(const device_name_t* name, uint8_t preferred_addr, const ca_identity_t* identity);

/**
 * Free all resources associated with the CA.
 * ecu_stop_ca() must have been called first.
 *
 * @ca  CA instance.
 */
void ca_destroy(ca_t* ca);

/**
 * Start the CA — creates its own socket on @ifname, claims an address,
 * and starts the RX, TX, and sensor threads.
 * Called internally by ecu_start_ca(). Not for use in main.c.
 *
 * @ca      CA instance.
 * @ifname  CAN interface name (e.g. "vcan0").
 */
int ca_start(ca_t* ca, const char* ifname);

/**
 * Stop the CA — signals threads to stop and waits for them to exit.
 * Called internally by ecu_stop_ca(). Not for use in main.c.
 */
void ca_stop(ca_t* ca);

/**
 * Returns the currently claimed address of the CA.
 * Returns J1939_IDLE_ADDR if no address has been claimed yet.
 */
uint8_t ca_get_claimed_addr(const ca_t* ca);

/**
 * Dispatch a received frame to this CA for processing.
 * Called internally by the CA's own RX thread.
 *
 * @ca        CA instance.
 * @pgn       PGN of the received frame.
 * @src_addr  Source address of the sender.
 * @buf       Payload buffer.
 * @len       Length of payload.
 */
void ca_receive(ca_t* ca, uint32_t pgn, uint8_t src_addr, const uint8_t* buf, size_t len);

#endif /* CA_H */

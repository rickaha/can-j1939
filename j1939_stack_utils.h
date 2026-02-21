/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef J1939_STACK_UTILS_H
#define J1939_STACK_UTILS_H

#include <stdint.h>
#include <linux/can/j1939.h>
#include <stddef.h>

/* STRUCTS */

/**
 * J1939 NAME — 64-bit ECU identifier used for Address Claiming.
 * Bit fields are ordered per SAE J1939/21.
 */
typedef union {
    struct {
        uint32_t identity_number : 21;
        uint16_t mfg_code        : 11;
        uint8_t  function_inst   : 5;
        uint8_t  function        : 8;
        uint8_t  reserved        : 1;
        uint8_t  vehicle_system  : 7;
        uint8_t  system_inst     : 4;
        uint8_t  industry_group  : 3;
        uint8_t  arbitrary_addr  : 1;
    } __attribute__((packed));
    uint64_t value;
} j1939_name_t;

/* SOCKET */

/**
 * Open a J1939 socket and bind to @ifname with the given NAME and address.
 * Initiates address claiming via the kernel J1939 stack.
 *
 * @ifname   CAN interface name (e.g. "vcan0").
 * @name     64-bit J1939 NAME for address claiming.
 * @addr     Preferred source address.
 *
 * Returns the socket fd on success, -1 on failure.
 */
int j1939_socket_open(const char *ifname, uint64_t name, uint8_t addr);

/* SEND */

/**
 * Generic J1939 send.
 *
 * Constructs the sockaddr_can destination from @pgn and @dest_addr, then
 * calls sendto(). The PGN format (PDU1 vs PDU2) is determined automatically:
 *   - PDU1 PGNs (bits 17-16 == 0) are sent unicast to @dest_addr.
 *   - PDU2 PGNs are always broadcast; @dest_addr is ignored.
 *
 * @sock       Bound J1939 socket fd.
 * @pgn        PGN number (24-bit).
 * @dest_addr  Destination SA for PDU1 PGNs; ignored for PDU2.
 * @payload    Pre-built payload buffer.
 * @len        Length of @payload in bytes.
 *
 * Returns 0 on success, -1 on failure (errno set by sendto).
 */
int j1939_send(int sock, uint32_t pgn, uint8_t dest_addr,
               const void *payload, size_t len);

#endif /* J1939_STACK_UTILS_H */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef PGN_DATA_H
#define PGN_DATA_H

#include "sensors.h"
#include <stddef.h>
#include <stdint.h>

#define REQUEST_QUEUE_SIZE 8

/* PGN NUMBERS */

#define PGN_59392 0x00E800U /* Acknowledgement (PDU1)          */
#define PGN_59904 0x00EA00U /* Request PGN (PDU1)              */
#define PGN_60928 0x00EE00U /* Address Claimed (PDU2)          */
#define PGN_64965 0x00FDC5U /* ECU Identification (PDU2)       */
#define PGN_65240 0x00FED8U /* Commanded Address (PDU2)        */
#define PGN_65242 0x00FEDAU /* Software Identification (PDU2)  */
#define PGN_65259 0x00FEEBU /* Component Identification (PDU2) */

/* STRUCTS */

/**
 * PGN 65259: Component Identification.
 * Application-level container.
 */
typedef struct {
    char make[32];
    char model[32];
    char serial[32];
    char unit[32];
} component_id_t;

/**
 * PGN 65242: Software Identification.
 * @version  Firmware version string (e.g. "1.0.0").
 */
typedef struct {
    char version[64];
} software_id_t;

/**
 * PGN 64965: ECU Identification.
 * All fields are ASCII strings delimited by '*' in the payload.
 */
typedef struct {
    char part_number[32];
    char serial[32];
    char location[32];
    char type[32];
} ecu_id_t;

/**
 * A single on-request entry pushed onto the request queue.
 */
typedef struct {
    uint32_t pgn;
    uint8_t requester_addr;
} pgn_request_t;

/**
 * Output of parse_request().
 * Fields are populated depending on which PGN was parsed.
 */
typedef struct {
    uint64_t name;
    uint32_t pgn;
    uint8_t new_addr;
} parsed_request_t;

/* INIT */

/**
 * Initialise pgn_data with device-specific identity.
 * Call once from main() before starting threads.
 *
 * @component_id   Device-specific component identification.
 * @software_id    Software identification strings.
 * @ecu_id         ECU identification strings.
 */
void pgn_data_init(const component_id_t* component_id, const software_id_t* software_id,
                   const ecu_id_t* ecu_id);

/* BUILDERS */

/**
 * Build a payload for any supported PGN.
 * Selects the correct builder based on @pgn.
 *
 * @pgn          PGN number to build payload for.
 * @values       Current sensor values (may be NULL for static PGNs).
 * @buf          Caller-supplied buffer to write the payload into.
 * @buf_len      Size of @buf in bytes.
 * @len          Written with the number of bytes produced on success.
 *
 * Returns 0 on success, -1 if PGN is unsupported or payload overflows.
 */
int build_payload(uint32_t pgn, const sensor_values_t* values, uint8_t* buf, size_t buf_len,
                  size_t* len);

/**
 * Build the PGN 59392 (Acknowledgement / NACK) payload.
 *
 * Produces an 8-byte NACK directed at @requester_addr for the
 * unsupported @requested_pgn.
 *
 * @requester_addr   SA of the node that sent the request.
 * @requested_pgn    The PGN that was requested but is not supported.
 * @buf              Caller-supplied buffer (must be >= 8 bytes).
 * @buf_len          Size of @buf in bytes.
 * @len              Written with 8 on success.
 *
 * Returns 0 on success, -1 if buf_len is less than 8.
 */
int build_pgn_59392_payload(uint8_t requester_addr, uint32_t requested_pgn, uint8_t* buf,
                            size_t buf_len, size_t* len);

/**
 * Build the PGN 65242 (Software Identification) payload.
 *
 * Format: 1 byte field count, followed by version string.
 *
 * @software_id   Source struct holding the version string.
 * @buf           Caller-supplied buffer to write the payload into.
 * @buf_len       Size of @buf in bytes.
 * @len           Written with the number of bytes produced on success.
 *
 * Returns 0 on success, -1 if the payload would overflow @buf_len.
 */
int build_pgn_65242_payload(const software_id_t* software_id, uint8_t* buf, size_t buf_len,
                            size_t* len);

/**
 * Build the PGN 64965 (ECU Identification) payload.
 *
 * Concatenates fields as PartNumber*Serial*Location*Type* into @buf.
 *
 * @ecu_id   Source struct holding the four identity strings.
 * @buf      Caller-supplied buffer to write the payload into.
 * @buf_len  Size of @buf in bytes.
 * @len      Written with the number of bytes produced on success.
 *
 * Returns 0 on success, -1 if the payload would overflow @buf_len.
 */
int build_pgn_64965_payload(const ecu_id_t* ecu_id, uint8_t* buf, size_t buf_len, size_t* len);

/**
 * Build the PGN 65259 (Component Identification) payload.
 *
 * Concatenates fields as Make*Model*Serial*Unit* into @payload_buf.
 *
 * @component_id      Source struct holding the four identity strings.
 * @payload_buf       Caller-supplied buffer to write the payload into.
 * @payload_buf_len   Size of @payload_buf in bytes.
 * @payload_len       Written with the number of bytes produced on success.
 *
 * Returns 0 on success, -1 if the payload would overflow @payload_buf_len.
 */
int build_pgn_65259_payload(const component_id_t* component_id, uint8_t* payload_buf,
                            size_t payload_buf_len, size_t* payload_len);

/* REQUESTS */

/**
 * Validate an incoming PGN request and push it onto the request queue.
 * Called when a PGN 59904 (Request PGN) is received.
 *
 * @requested_pgn    The PGN number being requested.
 * @requester_addr   Source address of the requesting node.
 * @queue            Caller-supplied request queue array.
 * @queue_count      Current number of entries in the queue. Updated on push.
 *
 * Returns 0 on success, -1 if the PGN is unsupported or the queue is full.
 */
int handle_request(uint32_t requested_pgn, uint8_t requester_addr, pgn_request_t* queue,
                   uint8_t* queue_count);

/* PARSERS */

/**
 * Parse an incoming request frame for any supported PGN.
 * Selects the correct parser based on @pgn and populates @request.
 *
 * @pgn      PGN number of the received frame.
 * @buf      Received payload buffer.
 * @buf_len  Length of @buf in bytes.
 * @request  Populated with parsed data on success.
 *
 * Returns 0 on success, -1 if PGN is unsupported or payload is malformed.
 */
int parse_request(uint32_t pgn, const uint8_t* buf, size_t buf_len, parsed_request_t* request);

/**
 * Parse PGN 59904 (Request PGN) payload.
 *
 * The payload is always 3 bytes encoding the requested PGN little-endian.
 *
 * @buf            Received payload buffer.
 * @buf_len        Length of @buf in bytes.
 * @request        Populated depending on which PGN was parsed.
 *
 * Returns 0 on success, -1 if buf_len is less than 3.
 */
int parse_pgn_59904_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request);

/**
 * Parse PGN 60928 (Address Claimed) payload.
 * The payload is always 8 bytes encoding the sender's J1939 NAME little-endian.
 *
 * @buf      Received payload buffer.
 * @buf_len  Length of @buf in bytes.
 * @request  request.name written with the sender's NAME on success.
 *
 * Returns 0 on success, -1 if buf_len is less than 8.
 */
int parse_pgn_60928_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request);

/**
 * Parse PGN 65240 (Commanded Address) payload.
 * The payload is 9 bytes: 8 bytes NAME little-endian + 1 byte new address.
 *
 * @buf      Received payload buffer.
 * @buf_len  Length of @buf in bytes.
 * @request  request.name and request.new_addr written on success.
 *
 * Returns 0 on success, -1 if buf_len is less than 9.
 */
int parse_pgn_65240_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request);

#endif /* PGN_DATA_H */

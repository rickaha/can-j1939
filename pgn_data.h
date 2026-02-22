/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef J1939_PGN_DATA_H
#define J1939_PGN_DATA_H

#include <stdint.h>
#include <stddef.h>

#define REQUEST_QUEUE_SIZE 8

/* PGN NUMBERS */

#define PGN_59904     0x00EA00U  /* Request PGN (PDU1)              */
#define PGN_65259     0x00FEEBU  /* Component Identification (PDU2) */

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
} j1939_component_id_t;

/**
 * A single on-request entry pushed onto the request queue.
 */
typedef struct {
  uint32_t pgn;
  uint8_t  requester_addr;
} pgn_request_t;

/* BUILDERS */

/**
 * Build the PGN 65259 payload.
 *
 * Concatenates fields as Make*Model*Serial*Unit* into @payload_buf.
 * The '*' delimiter is appended by this function — callers store plain strings.
 *
 * @component_id      Source struct holding the four identity strings.
 * @payload_buf       Caller-supplied buffer to write the payload into.
 * @payload_buf_len   Size of @payload_buf in bytes.
 * @payload_len       Written with the number of bytes produced on success.
 *
 * Returns 0 on success, -1 if the payload would overflow @payload_buf_len.
 */
int build_pgn_65259_payload(const j1939_component_id_t *component_id,
                            uint8_t *payload_buf, size_t payload_buf_len,
                            size_t *payload_len);

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
int handle_request(uint32_t requested_pgn, uint8_t requester_addr,
                   pgn_request_t *queue, uint8_t *queue_count);

#endif /* J1939_PGN_DATA_H */

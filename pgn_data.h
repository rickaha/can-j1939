/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef J1939_PGN_DATA_H
#define J1939_PGN_DATA_H

#include <stdint.h>
#include <stddef.h>

/* PGN NUMBERS */

#define PGN_REQUEST   0x00EA00U  /* 59904  — Request PGN (PDU1)             */
#define PGN_COMP_ID   0x00FEEBU  /* 65259  — Component Identification (PDU2) */

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

#endif /* J1939_PGN_DATA_H */

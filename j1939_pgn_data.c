/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include <stdio.h>
#include <string.h>
#include "j1939_pgn_data.h"

/* BUILDERS */

int build_pgn_65259_payload(const j1939_component_id_t *component_id,
                               uint8_t *payload_buf, size_t payload_buf_len,
                               size_t *payload_len) {
    memset(payload_buf, 0, payload_buf_len);

    /*
     * Build the format: Make*Model*Serial*Unit*
     * snprintf returns the number of bytes that would have been written
     * excluding the null terminator, so we can detect overflow cleanly.
     */
    int written = snprintf((char *)payload_buf, payload_buf_len, "%s*%s*%s*%s*",
                           component_id->make,
                           component_id->model,
                           component_id->serial,
                           component_id->unit);

    if (written < 0 || (size_t)written >= payload_buf_len) {
        fprintf(stderr, "build_pgn_65259_payload: payload overflow\n");
        return -1;
    }

    *payload_len = (size_t)written;
    return 0;
}

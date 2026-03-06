/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "pgn_data.h"
#include <endian.h>
#include <stdio.h>
#include <string.h>

/* INIT */

static component_id_t component_id = {0};

void pgn_data_init(const component_id_t* id) {
    component_id = *id;
}

/* BUILDERS */

int build_pgn_65259_payload(const component_id_t* component_id, uint8_t* payload_buf,
                            size_t payload_buf_len, size_t* payload_len) {
    memset(payload_buf, 0, payload_buf_len);

    /*
     * Build the format: Make*Model*Serial*Unit*
     * snprintf returns the number of bytes that would have been written
     * excluding the null terminator, so we can detect overflow cleanly.
     */
    int written = snprintf((char*)payload_buf, payload_buf_len, "%s*%s*%s*%s*", component_id->make,
                           component_id->model, component_id->serial, component_id->unit);

    if (written < 0 || (size_t)written >= payload_buf_len) {
        fprintf(stderr, "build_pgn_65259_payload: payload overflow\n");
        return -1;
    }

    *payload_len = (size_t)written;
    return 0;
}

/* DISPATCH */

int build_payload(uint32_t pgn, const sensor_values_t* values, uint8_t* buf, size_t buf_len,
                  size_t* len) {
    (void)values; /* unused until sensor PGNs are implemented */

    switch (pgn) {
    case PGN_65259:
        return build_pgn_65259_payload(&component_id, buf, buf_len, len);
    default:
        fprintf(stderr, "build_payload: unsupported PGN 0x%05X\n", pgn);
        return -1;
    }
}

/* REQUESTS */

int handle_request(uint32_t requested_pgn, uint8_t requester_addr, pgn_request_t* queue,
                   uint8_t* queue_count) {
    /* Check if the requested PGN is in the supported table. */
    switch (requested_pgn) {
    case PGN_65259:
        // Add more cases here as you implement them
        break;

    default:
        fprintf(stderr, "handle_request: unsupported PGN 0x%05X requested\n", requested_pgn);
        return -1;
    }

    /* Guard against queue overflow. */
    if (*queue_count >= REQUEST_QUEUE_SIZE) {
        fprintf(stderr, "handle_request: queue full, dropping PGN 0x%05X\n", requested_pgn);
        return -1;
    }

    /* Push the request onto the queue. */
    queue[*queue_count].pgn = requested_pgn;
    queue[*queue_count].requester_addr = requester_addr;
    (*queue_count)++;

    return 0;
}

/* PARSERS */

int parse_request(uint32_t pgn, const uint8_t* buf, size_t buf_len, parsed_request_t* request) {
    switch (pgn) {
    case PGN_59904:
        return parse_pgn_59904_payload(buf, buf_len, request);
    case PGN_60928:
        return parse_pgn_60928_payload(buf, buf_len, request);
    default:
        fprintf(stderr, "parse_payload: unsupported PGN 0x%05X\n", pgn);
        return -1;
    }
}

int parse_pgn_59904_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request) {
    if (buf_len < 3) {
        fprintf(stderr, "parse_pgn_59904_payload: payload too short (%zu bytes)\n", buf_len);
        return -1;
    }

    /* PGN is encoded little-endian in 3 bytes. */
    request->pgn = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16);

    return 0;
}

int parse_pgn_60928_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request) {
    if (buf_len < 8) {
        fprintf(stderr, "parse_pgn_ee00_payload: payload too short (%zu bytes)\n", buf_len);
        return -1;
    }

    /* NAME is encoded little-endian in 8 bytes. */
    uint64_t name = 0;
    memcpy(&name, buf, sizeof(name));
    request->name = le64toh(name);

    return 0;
}

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
static software_id_t software_id = {0};

void pgn_data_init(const component_id_t* cid, const software_id_t* sid) {
    component_id = *cid;
    software_id = *sid;
}

/* BUILDERS */

int build_pgn_59392_payload(uint8_t requester_addr, uint32_t requested_pgn, uint8_t* buf,
                            size_t buf_len, size_t* len) {
    if (buf_len < 8) {
        fprintf(stderr, "build_pgn_59392_payload: buffer too small\n");
        return -1;
    }

    /*
     * Acknowledgement frame layout (8 bytes):
     *   Byte 0: Control byte — 0x01 = NACK
     *   Byte 1: Group function value — 0xFF (not used)
     *   Byte 2: 0xFF (reserved)
     *   Byte 3: 0xFF (reserved)
     *   Byte 4: Address of requester
     *   Bytes 5-7: Requested PGN little-endian
     */
    buf[0] = 0x01; /* NACK */
    buf[1] = 0xFF;
    buf[2] = 0xFF;
    buf[3] = 0xFF;
    buf[4] = requester_addr;
    buf[5] = (uint8_t)(requested_pgn & 0xFF);
    buf[6] = (uint8_t)((requested_pgn >> 8) & 0xFF);
    buf[7] = (uint8_t)((requested_pgn >> 16) & 0xFF);

    *len = 8;
    return 0;
}

int build_pgn_65242_payload(const software_id_t* sid, uint8_t* buf, size_t buf_len, size_t* len) {
    if (buf_len < 2) {
        fprintf(stderr, "build_pgn_65242_payload: buffer too small\n");
        return -1;
    }

    /*
     * Software Identification layout:
     *   Byte 0: Number of fields (1 — version string only)
     *   Bytes 1..N: Version string (not null-terminated in payload)
     */
    buf[0] = 1; /* one field */
    int written = snprintf((char*)buf + 1, buf_len - 1, "%s", sid->version);
    if (written < 0 || (size_t)written >= buf_len - 1) {
        fprintf(stderr, "build_pgn_65242_payload: payload overflow\n");
        return -1;
    }

    *len = (size_t)written + 1; /* +1 for the field count byte */
    return 0;
}

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
    case PGN_65242:
        return build_pgn_65242_payload(&software_id, buf, buf_len, len);
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
    case PGN_65242:
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
    case PGN_65240:
        return parse_pgn_65240_payload(buf, buf_len, request);
    default:
        fprintf(stderr, "parse_request: unsupported PGN 0x%05X\n", pgn);
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
        fprintf(stderr, "parse_pgn_60928_payload: payload too short (%zu bytes)\n", buf_len);
        return -1;
    }

    /* NAME is encoded little-endian in 8 bytes. */
    uint64_t name = 0;
    memcpy(&name, buf, sizeof(name));
    request->name = le64toh(name);

    return 0;
}

int parse_pgn_65240_payload(const uint8_t* buf, size_t buf_len, parsed_request_t* request) {
    if (buf_len < 9) {
        fprintf(stderr, "parse_pgn_65240_payload: payload too short (%zu bytes)\n", buf_len);
        return -1;
    }

    /* NAME is encoded little-endian in bytes 0-7, new address in byte 8. */
    uint64_t name = 0;
    memcpy(&name, buf, sizeof(name));
    request->name = le64toh(name);
    request->new_addr = buf[8];

    return 0;
}

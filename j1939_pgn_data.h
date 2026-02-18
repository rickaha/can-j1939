/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef J1939_PGN_DATA_H
#define J1939_PGN_DATA_H

#include <stdint.h>

/**
 * PGN 65259: Component Identification
 * Format: Make*Model*Serial*Unit*
 * Note: The '*' is the J1939 defined delimiter.
 */
typedef struct {
    char make[10];
    char model[15];
    char serial[15];
    char unit[10];
} __attribute__((packed)) j1939_component_id_t;

#endif

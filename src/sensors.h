/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

/* STRUCTS */

/**
 * Device-specific sensor values.
 * Add a field for each sensor wired up on this device.
 */
typedef struct {
    float ambient_temp; /* SPN 171 — ambient air temperature, degrees Celsius */
} sensor_values_t;

/**
 * Describes a single sensor polling task.
 * Declare one entry per sensor in the task table in ecu.c.
 *
 * @poll_rate_ms   How often this sensor should be polled in milliseconds.
 * @last_poll_ms   Timestamp of last poll (monotonic). Updated by sensors_poll().
 * @read           Reads from hardware into a caller-supplied raw buffer.
 *                 Returns 0 on success, -1 on error.
 * @write          Writes the raw buffer into the correct field of sensor_values_t.
 *                 Called under the sensors mutex. Returns 0 on success, -1 on error.
 */
typedef struct {
    uint32_t poll_rate_ms;
    uint64_t last_poll_ms;
    int (*read)(void* buf);
    int (*write)(sensor_values_t* sensors, const void* buf);
} sensor_task_t;

/**
 * Read ambient temperature.
 * Writes a float (degrees Celsius) into buf.
 * Returns 0 on success, -1 on error.
 */
int ambient_temp_read(void* buf);

/**
 * Write temperature value into sensor_values_t.
 * Reads a float from buf and stores it in sensors->ambient_temp.
 * Returns 0 on success, -1 on error.
 */
int ambient_temp_write(sensor_values_t* sensors, const void* buf);

#endif /* SENSORS_H */

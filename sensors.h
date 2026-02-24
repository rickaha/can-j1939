/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <pthread.h>
#include <stdint.h>

/* STRUCTS */

/**
 * Describes a single sensor polling task.
 * Declare one entry per sensor in the task table in ecu.c.
 *
 * @poll_rate_ms   How often this sensor should be polled in milliseconds.
 * @last_poll_ms   Timestamp of last poll (monotonic). Updated by sensors_poll().
 * @read           Function pointer to the sensor read function.
 * @value          Pointer to the corresponding field in sensor_values_t.
 * @mutex          Pointer to the mutex protecting sensor_values_t.
 */
typedef struct {
    uint32_t poll_rate_ms;
    uint32_t last_poll_ms;
    int (*read)(void* value);
    void* value;
    pthread_mutex_t* mutex;
} sensor_task_t;

/* READ */

/* Sensor read functions go here as they are implemented.
 * e.g. int sensor_temperature_read(void *value);
 *      int sensor_gps_read(void *value);          */

#endif /* SENSORS_H */

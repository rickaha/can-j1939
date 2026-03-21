/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Rickard Häll
 */
#include "sensors.h"
#include <string.h>

/*
 * For now we return a fake value that increments by 0.5°C each call,
 * wrapping from 0 to 50°C, so we can verify the full pipeline in candump.
 */
int ambient_temp_read(void* buf) {
    static float fake_temp = 20.0f;

    /* TODO: replace with real API call, for example:
     *   s8 result = bno055_convert_float_temp_celsius(&fake_temp);
     *   if (result != BNO055_SUCCESS) return -1; */
    fake_temp += 0.5f;
    if (fake_temp > 50.0f)
        fake_temp = 0.0f;

    memcpy(buf, &fake_temp, sizeof(float));
    return 0;
}

int ambient_temp_write(sensor_values_t* sensors, const void* buf) {
    memcpy(&sensors->ambient_temp, buf, sizeof(float));
    return 0;
}

#pragma once
#include <stdint.h>
#include <stddef.h>

#define OST_PIXELS 4
#define OST_FRAME_BYTES (OST_PIXELS * 3)
#define OST_PHASE_US 3
#define OST_LATCH_US 3500

/* OptoSupply: B, G, R bytes; MSB first, including bit zero. */
static inline void ost_encode(const struct led_rgb pixels[OST_PIXELS],
                              uint8_t bytes[OST_FRAME_BYTES]) {
    for (size_t i = 0; i < OST_PIXELS; i++) {
        bytes[3 * i] = pixels[i].b;
        bytes[3 * i + 1] = pixels[i].g;
        bytes[3 * i + 2] = pixels[i].r;
    }
}

/* Emit one active level and one midpoint for each of the 96 bits. */
#define OST_SEND_BYTES(bytes, high, low, midpoint, delay) do { \
    for (size_t ost_i = 0; ost_i < OST_FRAME_BYTES; ost_i++) { \
        for (uint8_t ost_mask = 0x80; ost_mask; ost_mask >>= 1) { \
            if ((bytes)[ost_i] & ost_mask) { high(); } else { low(); } \
            delay(OST_PHASE_US); \
            midpoint(); \
            delay(OST_PHASE_US); \
        } \
    } \
} while (0)

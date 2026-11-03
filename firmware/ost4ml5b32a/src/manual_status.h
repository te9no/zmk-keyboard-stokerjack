#pragma once
#include <zephyr/drivers/led_strip.h>

#define SJ_FRAME_MS 20
#define SJ_FRAMES 120

enum sj_display { SJ_BATTERY, SJ_LINK, SJ_NUM, SJ_CAPS, SJ_FN_LAYER, SJ_PROFILE };
struct sj_status {
    enum sj_display display;
    int battery, profile;
    bool usb, connected, caps;
};

static struct led_rgb sj_pixel(const struct sj_status *s, int i, unsigned frame)
{
    const struct led_rgb black = {0};
    switch (s->display) {
    case SJ_BATTERY: {
        if (s->battery < 0) return (struct led_rgb){.r=4, .b=6};
        int count = s->battery ? (s->battery + 24) / 25 : 1;
        if (i >= count) return black;
        if (s->battery <= 25) return (struct led_rgb){.r=16};
        if (s->battery <= 50) return (struct led_rgb){.r=14, .g=6};
        return (struct led_rgb){.g=14};
    }
    case SJ_LINK:
    case SJ_PROFILE:
        if (!s->connected && (frame / 20) % 2) return black;
        if (s->usb) return (struct led_rgb){.r=8, .g=8, .b=8};
        if (s->profile == i || (s->profile == 4 && (i == 0 || i == 3)))
            return (struct led_rgb){.b=16};
        return black;
    case SJ_FN_LAYER:
        return i == 0 ? (struct led_rgb){.r=8, .b=12} : black;
    case SJ_NUM:
        return i < 2 ? (struct led_rgb){.r=12,.g=6} : black;
    case SJ_CAPS:
        return s->caps ? (struct led_rgb){.g=12} : (struct led_rgb){.r=12,.g=5};
    }
    return black;
}

/* Stagger the fade-in, then hold the reading and fade out together. */
static void sj_render(struct led_rgb pixels[4], const struct sj_status *s, unsigned frame)
{
    for (int i = 0; i < 4; i++) {
        unsigned delay = i * 3;
        unsigned rise = frame > delay ? frame - delay : 0;
        unsigned fall = frame < SJ_FRAMES ? SJ_FRAMES - 1 - frame : 0;
        unsigned level = MIN(12U, MIN(rise, fall));
        unsigned blend = level * level * (36U - 2U * level);
        struct led_rgb c = sj_pixel(s, i, frame);
        pixels[i].r = (c.r * blend + 864) / 1728;
        pixels[i].g = (c.g * blend + 864) / 1728;
        pixels[i].b = (c.b * blend + 864) / 1728;
    }
}

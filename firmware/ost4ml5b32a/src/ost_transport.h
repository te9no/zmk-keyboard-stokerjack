#pragma once
#include <zephyr/drivers/led_strip.h>
void ost_data_release(void);
int ost_write(const struct led_rgb pixels[4]);

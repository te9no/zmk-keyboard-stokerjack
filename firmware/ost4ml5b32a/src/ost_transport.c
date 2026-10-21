#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <hal/nrf_gpio.h>
#include "ost_transport.h"
#include "ost_protocol.h"

#define OST_DATA_PIN 27 /* P0.27, via existing R8=100 ohms */
BUILD_ASSERT(CONFIG_SOC_NRF52840, "This transport targets Stokerjack nRF52840");

void ost_data_release(void) {
    /* R11/R12 generate VDD/2. Disconnect input buffer at the analog midpoint;
     * internal pulls must stay off. With LED power off this also prevents backfeed. */
    nrf_gpio_cfg(OST_DATA_PIN, NRF_GPIO_PIN_DIR_INPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
}
static inline void high(void) {
    nrf_gpio_pin_set(OST_DATA_PIN);
    nrf_gpio_cfg_output(OST_DATA_PIN);
}
static inline void low(void) {
    nrf_gpio_pin_clear(OST_DATA_PIN);
    nrf_gpio_cfg_output(OST_DATA_PIN);
}
int ost_write(const struct led_rgb pixels[4]) {
    uint8_t bytes[OST_FRAME_BYTES];
    ost_encode(pixels, bytes);
    ost_data_release();
    k_busy_wait(OST_PHASE_US);
    /* Only the data burst is atomic: nominal 576 us plus GPIO/software overhead.
     * The >=3 ms latch is outside the lock so USB/BLE can run during it. */
    unsigned int key = irq_lock();
    OST_SEND_BYTES(bytes, high, low, ost_data_release, k_busy_wait);
    irq_unlock(key);
    k_usleep(OST_LATCH_US);
    return 0;
}

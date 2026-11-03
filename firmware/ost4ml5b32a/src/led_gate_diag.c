/* The status build shares the diagnostic driver and switched LED supply. */
#define DT_DRV_COMPAT te9no_stokerjack_led
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include "ost_transport.h"

#ifdef STOKERJACK_MANUAL_STATUS
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include "manual_status.h"

static atomic_t requested_display = ATOMIC_INIT(SJ_BATTERY);
static atomic_t battery_percent = ATOMIC_INIT(-1);

static int on_battery(const zmk_event_t *event)
{
    const struct zmk_battery_state_changed *battery = as_zmk_battery_state_changed(event);
    atomic_set(&battery_percent, battery->state_of_charge);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stokerjack_manual_battery, on_battery);
ZMK_SUBSCRIPTION(stokerjack_manual_battery, zmk_battery_state_changed);

static void fill_pixels(struct led_rgb pixels[4])
{
    struct sj_status status = {.display = atomic_get(&requested_display),
                               .battery = atomic_get(&battery_percent)};
    if (status.display == SJ_LINK) {
        status.usb = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
        status.profile = zmk_ble_active_profile_index();
        status.connected = status.usb || zmk_ble_active_profile_is_connected();
    }
    for (int i = 0; i < 4; ++i) {
        /* The first LED on DIN is the rightmost LED on the assembled board. */
        int position_from_left = 3 - i;
        pixels[i] = sj_pixel(&status, position_from_left, 0);
        /* Amber marks a disconnected slot without repeated LED transfers. */
        if (status.display == SJ_LINK && !status.connected && pixels[i].b) {
            pixels[i] = (struct led_rgb){.r=7, .g=3};
        }
    }
}
#define DISPLAY_DURATION K_SECONDS(2)
#else
static void fill_pixels(struct led_rgb pixels[4])
{
    for (int i = 0; i < 4; ++i) pixels[i].r = 8;
}
#define DISPLAY_DURATION K_SECONDS(15)
#endif

static const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(gpio1));
enum { LED_GATE_PIN = 9, FN_LAYER = 1, NUM_LAYER = 2, BT_LAYER = 3 };

#ifdef STOKERJACK_MANUAL_STATUS
static void show_battery_after_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(fn_followup, show_battery_after_fn);
#endif

static void gate_off(struct k_work *work)
{
    ost_data_release();
    gpio_pin_set_raw(port, LED_GATE_PIN, 1);
}
K_WORK_DELAYABLE_DEFINE(switch_off, gate_off);

static void show_pixels(struct k_work *work)
{
    struct led_rgb pixels[4] = {0};
    ost_data_release();
    if (gpio_pin_set_raw(port, LED_GATE_PIN, 0) < 0) return;
    if (k_work_reschedule(&switch_off, DISPLAY_DURATION) < 0) {
        gate_off(NULL);
        return;
    }
    k_msleep(10);
    fill_pixels(pixels);
    ost_write(pixels);
#ifdef STOKERJACK_MANUAL_STATUS
    if (atomic_get(&requested_display) == SJ_FN_LAYER) {
        k_work_reschedule(&fn_followup, K_MSEC(450));
    }
#endif
}
K_WORK_DEFINE(display_work, show_pixels);

#ifdef STOKERJACK_MANUAL_STATUS
/* Serial chain index zero is on the right. Stagger the same dawn pulse
 * across the four LEDs, advancing one frame per system workqueue visit. */
static const struct led_rgb dawn_pulse[] = {
    {.b=4}, {.r=3, .b=8}, {.r=7, .b=7}, {.r=12, .g=2, .b=3},
    {.r=14, .g=6}, {.r=9, .g=4}, {.r=4, .g=1},
};
enum { DAWN_STAGGER = 2, DAWN_FRAMES = ARRAY_SIZE(dawn_pulse) + 3 * DAWN_STAGGER };
static atomic_t boot_animation = ATOMIC_INIT(1);
static int boot_frame = -1;
static void advance_dawn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(dawn_work, advance_dawn);

static void advance_dawn(struct k_work *work)
{
    if (!atomic_get(&boot_animation)) return;
    if (boot_frame < 0) {
        ost_data_release();
        if (gpio_pin_set_raw(port, LED_GATE_PIN, 0) < 0) return;
        boot_frame = 0;
        k_work_reschedule(&dawn_work, K_MSEC(10));
        return;
    }
    if (boot_frame == DAWN_FRAMES) {
        atomic_clear(&boot_animation);
        gate_off(NULL);
        return;
    }
    struct led_rgb pixels[4] = {0};
    for (int i = 0; i < 4; ++i) {
        int phase = boot_frame - (3 - i) * DAWN_STAGGER;
        if (phase >= 0 && phase < (int)ARRAY_SIZE(dawn_pulse)) pixels[i] = dawn_pulse[phase];
    }
    ost_write(pixels);
    ++boot_frame;
    k_work_reschedule(&dawn_work, K_MSEC(65));
}

static void show_battery_after_fn(struct k_work *work)
{
    if (atomic_cas(&requested_display, SJ_FN_LAYER, SJ_BATTERY)) {
        k_work_submit(&display_work);
    }
}

static void request_status(enum sj_display display)
{
    atomic_clear(&boot_animation);
    k_work_cancel_delayable(&dawn_work);
    k_work_cancel_delayable(&fn_followup);
    atomic_set(&requested_display, display);
    k_work_submit(&display_work);
}
#endif

static int on_layer(const zmk_event_t *event)
{
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(event);
#ifdef STOKERJACK_MANUAL_STATUS
    if (!layer) return ZMK_EV_EVENT_BUBBLE;
    unsigned int active = zmk_keymap_highest_layer_active();
    if ((layer->state && layer->layer == active) || (!layer->state && layer->layer > active)) {
        switch (active) {
        case FN_LAYER:  request_status(SJ_FN_LAYER); break;
        case NUM_LAYER: request_status(SJ_NUM); break;
        case BT_LAYER:  request_status(SJ_BT_LAYER); break;
        }
    }
#else
    if (layer && layer->layer == FN_LAYER && layer->state) k_work_submit(&display_work);
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stokerjack_fn_diag, on_layer);
ZMK_SUBSCRIPTION(stokerjack_fn_diag, zmk_layer_state_changed);

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event)
{
#ifdef STOKERJACK_MANUAL_STATUS
    request_status(SJ_LINK);
#else
    k_work_submit(&display_work);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}
static int init(const struct device *dev)
{
    if (!device_is_ready(port)) return -ENODEV;
    ost_data_release();
    int err = gpio_pin_configure(port, LED_GATE_PIN, GPIO_OUTPUT_HIGH);
#ifdef STOKERJACK_MANUAL_STATUS
    if (!err) k_work_schedule(&dawn_work, K_MSEC(180));
#endif
    return err;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed, .binding_released = released
};
BEHAVIOR_DT_INST_DEFINE(0, init, NULL, NULL, NULL, POST_KERNEL, 99, &api);

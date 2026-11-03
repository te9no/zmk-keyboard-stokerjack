/* The status build shares the diagnostic driver and switched LED supply. */
#define DT_DRV_COMPAT te9no_stokerjack_led
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include "ost_transport.h"

LOG_MODULE_REGISTER(stokerjack_gate, CONFIG_ZMK_LOG_LEVEL);

#ifdef STOKERJACK_MANUAL_STATUS
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include "manual_status.h"

static atomic_t requested_display = ATOMIC_INIT(SJ_BATTERY);
static atomic_t last_profile = ATOMIC_INIT(-1);
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
    if (status.display == SJ_LINK || status.display == SJ_PROFILE) {
        status.usb = status.display == SJ_LINK && zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
        status.profile = zmk_ble_active_profile_index();
        status.connected = status.usb || zmk_ble_active_profile_is_connected();
    }
    if (status.display == SJ_PROFILE) LOG_DBG("Displaying wireless slot %d", status.profile + 1);
    for (int i = 0; i < 4; ++i) {
        /* The first LED on DIN is the rightmost LED on the assembled board. */
        int position_from_left = 3 - i;
        pixels[i] = sj_pixel(&status, position_from_left, 0);
        /* Amber marks a disconnected slot without repeated LED transfers. */
        if ((status.display == SJ_LINK || status.display == SJ_PROFILE) && !status.connected && pixels[i].b) {
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

/* All gate transitions after init run on the same system workqueue. */
static bool rail_on, needs_sync;
static int64_t rail_ready_at;

static void gate_off(struct k_work *work)
{
    ost_data_release();
    int err = gpio_pin_set_raw(port, LED_GATE_PIN, 1);
    if (err) LOG_ERR("LED gate off failed (%d)", err);
    rail_on = false;
}
K_WORK_DELAYABLE_DEFINE(switch_off, gate_off);

/* Wait for the switched rail without blocking the wireless workqueue. */
static bool prepare_strip(struct k_work_delayable *resume)
{
    if (!rail_on) {
        ost_data_release();
        int err = gpio_pin_configure(port, LED_GATE_PIN, GPIO_OUTPUT_LOW);
        if (err) {
            LOG_ERR("LED gate enable failed (%d)", err);
            return false;
        }
        rail_on = true;
        needs_sync = true;
        rail_ready_at = k_uptime_get() + 50;
    }
    int64_t remaining = rail_ready_at - k_uptime_get();
    if (remaining > 0) {
        k_work_reschedule(resume, K_MSEC(remaining));
        return false;
    }
    if (needs_sync) {
        const struct led_rgb black[4] = {0};
        ost_write(black);  /* Full frame and latch before the first visible color. */
        needs_sync = false;
    }
    return true;
}

static void show_pixels(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(display_work, show_pixels);
static void show_pixels(struct k_work *work)
{
    struct led_rgb pixels[4] = {0};
    if (!prepare_strip(&display_work)) return;
    if (k_work_reschedule(&switch_off, DISPLAY_DURATION) < 0) {
        gate_off(NULL);
        return;
    }
    fill_pixels(pixels);
    ost_write(pixels);
    LOG_DBG("LED status frame sent");
#ifdef STOKERJACK_MANUAL_STATUS
    if (atomic_get(&requested_display) == SJ_FN_LAYER) {
        k_work_reschedule(&fn_followup, K_MSEC(450));
    }
#endif
}


#ifdef STOKERJACK_MANUAL_STATUS
/* Serial chain index zero is on the right. Stagger the same dawn pulse
 * across the four LEDs, advancing one frame per system workqueue visit. */
static const struct led_rgb dawn_pulse[] = {
    {.b=4}, {.r=3, .b=8}, {.r=7, .b=7}, {.r=12, .g=2, .b=3},
    {.r=14, .g=6}, {.r=9, .g=4}, {.r=4, .g=1},
};
enum { DAWN_STAGGER = 2, DAWN_FRAMES = ARRAY_SIZE(dawn_pulse) + 3 * DAWN_STAGGER };
static atomic_t boot_animation = ATOMIC_INIT(1);
static int boot_frame;
static void advance_dawn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(dawn_work, advance_dawn);

static void advance_dawn(struct k_work *work)
{
    if (!atomic_get(&boot_animation)) return;
    if (!prepare_strip(&dawn_work)) return;
    if (boot_frame == 0) {
        atomic_cas(&last_profile, -1, zmk_ble_active_profile_index());
        LOG_DBG("LED startup frames begin");
    }
    if (boot_frame == DAWN_FRAMES) {
        LOG_DBG("LED startup frames complete");
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
        k_work_reschedule(&display_work, K_NO_WAIT);
    }
}

static void request_status(enum sj_display display)
{
    atomic_cas(&last_profile, -1, zmk_ble_active_profile_index());
    atomic_clear(&boot_animation);
    k_work_cancel_delayable(&dawn_work);
    k_work_cancel_delayable(&fn_followup);
    atomic_set(&requested_display, display);
    k_work_reschedule(&display_work, K_NO_WAIT);
}

static int on_profile(const zmk_event_t *event)
{
    int profile = as_zmk_ble_active_profile_changed(event)->index;
    int previous = atomic_set(&last_profile, profile);
    /* Reconnect notifications repeat the same index; keep idle LEDs asleep. */
    if (previous >= 0 && previous != profile) request_status(SJ_PROFILE);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stokerjack_profile_led, on_profile);
ZMK_SUBSCRIPTION(stokerjack_profile_led, zmk_ble_active_profile_changed);
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
        case BT_LAYER:  request_status(SJ_PROFILE); break;
        }
    }
#else
    if (layer && layer->layer == FN_LAYER && layer->state) k_work_reschedule(&display_work, K_NO_WAIT);
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
    k_work_reschedule(&display_work, K_NO_WAIT);
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
    if (err) LOG_ERR("LED gate init failed (%d)", err);
    else k_work_schedule(&dawn_work, K_MSEC(180));
#endif
    return err;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed, .binding_released = released
};
BEHAVIOR_DT_INST_DEFINE(0, init, NULL, NULL, NULL, POST_KERNEL, 99, &api);

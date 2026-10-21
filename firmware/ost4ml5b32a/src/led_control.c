/* Status animations own the LED bus and its switched supply. */
#define DT_DRV_COMPAT te9no_stokerjack_led
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/ble.h>
#include <zmk/battery.h>
#include <zmk/endpoints.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/position_state_changed.h>
#include "status_animation.h"
#include "ost_transport.h"

LOG_MODULE_REGISTER(stokerjack_led, CONFIG_ZMK_LOG_LEVEL);
static const struct device *const port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *const port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static atomic_t awake = ATOMIC_INIT(1), generation, last_show;
static atomic_t battery_valid, battery_level;
static bool powered, ready;
static int frame = SJ_FRAMES, seen_generation;
K_MUTEX_DEFINE(sequence_lock);
static void update(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(led_work, update);

static int data_release(void) {
    ost_data_release();
    return 0;
}
static void power_off(void) {
    if (powered) {
        struct led_rgb black[4] = {0};
        int err = ost_write(black);
        if (err) LOG_WRN("Black frame: %d", err);
    }
    /* Release DIN before removing LED power. */
    if (data_release() == 0) {
        k_busy_wait(80);
        if (gpio_pin_set_raw(port1, 9, 1) == 0) powered = false;
    }
}
static void show(void) {
    atomic_set(&last_show, k_uptime_get_32());
    atomic_inc(&generation);
    k_work_reschedule(&led_work, K_NO_WAIT);
}
static void update(struct k_work *work) {
    if (!ready) return;
    k_mutex_lock(&sequence_lock, K_FOREVER);
    uint32_t frame_started = k_uptime_get_32();
    int gen = atomic_get(&generation);
    if (gen != seen_generation) { seen_generation = gen; frame = 0; }
    if (!atomic_get(&awake) || frame >= SJ_FRAMES) {
        frame = SJ_FRAMES;
        power_off();
        k_mutex_unlock(&sequence_lock);
        return;
    }
    int err = 0;
    if (!powered) {
        err = data_release();
        if (err) goto fail;
        err = gpio_pin_set_raw(port1, 9, 0);
        if (err) goto fail;
        powered = true;
        k_msleep(10);
    }
    struct led_rgb pixels[4] = {0};
    struct sj_status status = {
        .transport = zmk_endpoint_get_selected().transport,
        .profile = zmk_ble_active_profile_index(),
        .battery = atomic_get(&battery_valid) ? atomic_get(&battery_level) : -1,
        .layer = zmk_keymap_highest_layer_active(),
        .caps = (zmk_hid_indicators_get_current_profile() & (1u << 1)) != 0,
    };
    sj_render(pixels, &status, frame++);
    err = ost_write(pixels);
    if (err) goto fail;
    uint32_t elapsed = k_uptime_get_32() - frame_started;
    k_work_reschedule(&led_work, K_MSEC(elapsed < SJ_FRAME_MS ? SJ_FRAME_MS - elapsed : 1));
    k_mutex_unlock(&sequence_lock);
    return;
fail:
    LOG_ERR("LED sequence failed: %d", err);
    frame = SJ_FRAMES;
    power_off();
    k_mutex_unlock(&sequence_lock);
}
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    show();
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}
static int status_event(const zmk_event_t *event) {
    const struct zmk_activity_state_changed *a = as_zmk_activity_state_changed(event);
    if (a) {
        atomic_set(&awake, a->state == ZMK_ACTIVITY_ACTIVE);
        if (a->state == ZMK_ACTIVITY_ACTIVE) show();
        else { k_work_cancel_delayable(&led_work); update(NULL); }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_battery_state_changed *b = as_zmk_battery_state_changed(event);
    if (b) {
        atomic_set(&battery_level, b->state_of_charge);
        atomic_set(&battery_valid, 1);
        /* Measurements alone do not wake the LEDs. */
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_position_state_changed *p = as_zmk_position_state_changed(event);
    if (p) {
        if (p->state && (uint32_t)(k_uptime_get_32() - atomic_get(&last_show)) >= 30000) show();
        return ZMK_EV_EVENT_BUBBLE;
    }
    show();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stokerjack_led_status, status_event);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_hid_indicators_changed);
ZMK_SUBSCRIPTION(stokerjack_led_status, zmk_position_state_changed);
static int init(const struct device *dev) {
    if (!device_is_ready(port0) || !device_is_ready(port1)) return -ENODEV;
    int err = data_release();
    if (err) return err;
    err = gpio_pin_configure(port1, 9, GPIO_OUTPUT_HIGH);
    if (err) return err;
    ready = true;
    return 0;
}
static const struct behavior_driver_api api = {.binding_pressed = pressed, .binding_released = released};
BEHAVIOR_DT_INST_DEFINE(0, init, NULL, NULL, NULL, POST_KERNEL, 99, &api);

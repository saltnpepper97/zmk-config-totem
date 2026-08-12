/* SPDX-License-Identifier: MIT */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>

#define TELEMETRY_INTERVAL K_MINUTES(1)

struct peripheral_battery_state {
    uint8_t level;
    bool known;
    bool connected;
};

static struct peripheral_battery_state
    batteries[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

#if IS_ENABLED(CONFIG_SETTINGS)
static uint8_t persisted_levels[ARRAY_SIZE(batteries)];

static int battery_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                    void *cb_arg) {
    const char *next;
    if (!settings_name_steq(name, "levels", &next) || next) {
        return -ENOENT;
    }
    if (len != sizeof(persisted_levels)) {
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, persisted_levels, sizeof(persisted_levels));
    if (rc < 0) {
        return rc;
    }

    for (uint8_t source = 0; source < ARRAY_SIZE(batteries); source++) {
        if (persisted_levels[source] <= 100 && persisted_levels[source] > 0) {
            batteries[source].level = persisted_levels[source];
            batteries[source].known = true;
        }
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(totem_battery, "totem_battery", NULL, battery_settings_load_cb,
                               NULL, NULL);

static void battery_settings_save_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    for (uint8_t source = 0; source < ARRAY_SIZE(batteries); source++) {
        persisted_levels[source] = batteries[source].known ? batteries[source].level : 0;
    }
    settings_save_one("totem_battery/levels", persisted_levels, sizeof(persisted_levels));
}

K_WORK_DELAYABLE_DEFINE(battery_settings_save_work, battery_settings_save_work_handler);
#endif

static void update_battery(uint8_t source, uint8_t level, bool connected) {
    struct peripheral_battery_state *battery = &batteries[source];

    if (level == 0) {
        battery->connected = false;
        return;
    }

    bool level_changed = !battery->known || battery->level != level;
    battery->known = true;
    battery->level = level;
    battery->connected = connected;

#if IS_ENABLED(CONFIG_SETTINGS)
    if (level_changed) {
        k_work_reschedule(&battery_settings_save_work, K_SECONDS(1));
    }
#endif
}

static void emit_battery(uint8_t source) {
    const struct peripheral_battery_state *battery = &batteries[source];

    if (!battery->known) {
        return;
    }

    printk("TOTEM_BATTERY v=1 source=%u level=%u connected=%u\n", source, battery->level,
           battery->connected ? 1 : 0);
}

static void telemetry_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(telemetry_work, telemetry_work_handler);

static void telemetry_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    for (uint8_t source = 0; source < ARRAY_SIZE(batteries); source++) {
        /* Read ZMK's authoritative split-central cache. This is the same pair
         * of fetched peripheral values exposed as GATT proxy characteristics
         * to tools such as zmk-battery-center. Do not depend solely on our
         * listener seeing the original event: the tray may start later and
         * the USB console may reconnect at any time.
         */
        uint8_t fetched_level = 0;
        if (zmk_split_central_get_peripheral_battery_level(source, &fetched_level) == 0 &&
            fetched_level > 0) {
            update_battery(source, fetched_level, true);
        }
        emit_battery(source);
    }

    k_work_schedule(&telemetry_work, TELEMETRY_INTERVAL);
}

static int battery_telemetry_listener(const zmk_event_t *event) {
    const struct zmk_peripheral_battery_state_changed *battery_event =
        as_zmk_peripheral_battery_state_changed(event);

    if (battery_event != NULL && battery_event->source < ARRAY_SIZE(batteries)) {
        /* ZMK emits level zero when a split peripheral disconnects. Preserve the
         * last genuine percentage so the host can show useful last-known data.
         */
        update_battery(battery_event->source, battery_event->state_of_charge,
                       battery_event->state_of_charge > 0);
        emit_battery(battery_event->source);

        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *position_event =
        as_zmk_position_state_changed(event);

    /* A key event proves that its originating peripheral is connected. Some
     * bonded reconnects do not produce a fresh BAS read after ZMK's synthetic
     * zero-level disconnect event, so use actual split activity as the source
     * of truth for connection recovery.
     */
    if (position_event != NULL && position_event->source < ARRAY_SIZE(batteries)) {
        struct peripheral_battery_state *battery = &batteries[position_event->source];
        if (battery->known && !battery->connected) {
            battery->connected = true;
            emit_battery(position_event->source);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(totem_battery_telemetry, battery_telemetry_listener);
ZMK_SUBSCRIPTION(totem_battery_telemetry, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(totem_battery_telemetry, zmk_position_state_changed);

static int battery_telemetry_init(void) {
    k_work_schedule(&telemetry_work, K_SECONDS(10));
    return 0;
}

SYS_INIT(battery_telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

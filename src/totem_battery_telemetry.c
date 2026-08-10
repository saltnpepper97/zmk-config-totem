/* SPDX-License-Identifier: MIT */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#define TELEMETRY_INTERVAL K_MINUTES(1)

struct peripheral_battery_state {
    uint8_t level;
    bool known;
    bool connected;
};

static struct peripheral_battery_state
    batteries[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

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
        emit_battery(source);
    }

    k_work_schedule(&telemetry_work, TELEMETRY_INTERVAL);
}

static int battery_telemetry_listener(const zmk_event_t *event) {
    const struct zmk_peripheral_battery_state_changed *battery_event =
        as_zmk_peripheral_battery_state_changed(event);

    if (battery_event == NULL || battery_event->source >= ARRAY_SIZE(batteries)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct peripheral_battery_state *battery = &batteries[battery_event->source];
    battery->known = true;

    /* ZMK emits level zero when a split peripheral disconnects. Preserve the
     * last genuine percentage so the host can show useful last-known data.
     */
    if (battery_event->state_of_charge == 0) {
        battery->connected = false;
    } else {
        battery->level = battery_event->state_of_charge;
        battery->connected = true;
    }
    emit_battery(battery_event->source);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(totem_battery_telemetry, battery_telemetry_listener);
ZMK_SUBSCRIPTION(totem_battery_telemetry, zmk_peripheral_battery_state_changed);

static int battery_telemetry_init(void) {
    k_work_schedule(&telemetry_work, K_SECONDS(10));
    return 0;
}

SYS_INIT(battery_telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* SPDX-License-Identifier: MIT */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zmk/battery.h>

#define BATTERY_HEARTBEAT_INTERVAL K_MINUTES(1)

static void battery_heartbeat_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_heartbeat_work, battery_heartbeat_work_handler);

static void battery_heartbeat_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t level = zmk_battery_state_of_charge();
    if (level > 0) {
        /* The BLE split transport fetches battery data through the standard
         * Battery Service, not ZMK's split event characteristic. Setting BAS
         * sends a GATT notification even when the value is unchanged, which
         * refreshes the central after bonded reconnects.
         */
        bt_bas_set_battery_level(level);
    }

    k_work_schedule(&battery_heartbeat_work, BATTERY_HEARTBEAT_INTERVAL);
}

static int battery_heartbeat_init(void) {
    /* ZMK samples the local battery immediately during its own startup. Give
     * that first sample time to complete before sending our initial repeat.
     */
    k_work_schedule(&battery_heartbeat_work, K_SECONDS(15));
    return 0;
}

SYS_INIT(battery_heartbeat_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

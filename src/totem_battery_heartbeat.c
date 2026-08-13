/* SPDX-License-Identifier: MIT */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zmk/battery.h>

#if IS_ENABLED(CONFIG_TOTEM_BATTERY_DIAGNOSTICS)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

static const struct device *const battery_sensor = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static void print_battery_diagnostics(void) {
    struct sensor_value voltage;
    struct sensor_value charge;

    int rc = sensor_sample_fetch_chan(battery_sensor, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (rc == 0) {
        rc = sensor_channel_get(battery_sensor, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
    }
    if (rc == 0) {
        rc = sensor_channel_get(battery_sensor, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &charge);
    }

    if (rc == 0) {
        int32_t millivolts = (voltage.val1 * 1000) + (voltage.val2 / 1000);
        printk("TOTEM_LOCAL_BATTERY v=1 millivolts=%d level=%d\n", millivolts, charge.val1);
    } else {
        printk("TOTEM_LOCAL_BATTERY v=1 error=%d\n", rc);
    }
}
#endif

#define BATTERY_HEARTBEAT_INTERVAL K_MINUTES(1)

static void battery_heartbeat_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_heartbeat_work, battery_heartbeat_work_handler);

static void battery_heartbeat_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

#if IS_ENABLED(CONFIG_TOTEM_BATTERY_DIAGNOSTICS)
    print_battery_diagnostics();
#endif

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

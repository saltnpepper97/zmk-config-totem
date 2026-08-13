/* SPDX-License-Identifier: MIT */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#if IS_ENABLED(CONFIG_TOTEM_BATTERY_DIAGNOSTICS)
#include <zephyr/sys/printk.h>
#endif

static const struct device *const battery_sensor = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static int read_battery(uint8_t *level, int32_t *millivolts) {
    struct sensor_value voltage;
    struct sensor_value charge;

    int rc = sensor_sample_fetch_chan(battery_sensor, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (rc == 0) {
        rc = sensor_channel_get(battery_sensor, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
    }
    if (rc == 0) {
        rc = sensor_channel_get(battery_sensor, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &charge);
    }

    if (rc != 0) {
        return rc;
    }

    *millivolts = (voltage.val1 * 1000) + (voltage.val2 / 1000);
    *level = charge.val1;
    return 0;
}

#define BATTERY_HEARTBEAT_INTERVAL K_MINUTES(1)

static void battery_heartbeat_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_heartbeat_work, battery_heartbeat_work_handler);

static void battery_heartbeat_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t level = 0;
    int32_t millivolts = 0;
    int rc = read_battery(&level, &millivolts);

#if IS_ENABLED(CONFIG_TOTEM_BATTERY_DIAGNOSTICS)
    if (rc == 0) {
        printk("TOTEM_LOCAL_BATTERY v=1 millivolts=%d level=%d\n", millivolts, level);
    } else {
        printk("TOTEM_LOCAL_BATTERY v=1 error=%d\n", rc);
    }
#endif

    if (rc == 0) {
        /* The BLE split transport fetches battery data through the standard
         * Battery Service, not ZMK's split event characteristic. Sample the
         * sensor here instead of repeating ZMK's cached percentage: ZMK pauses
         * its own battery timer while idle, which can otherwise preserve a
         * charging-time value indefinitely. Setting BAS sends a notification
         * even when the value is unchanged and refreshes bonded reconnects.
         *
         * ZMK's split central also uses level 0 as its disconnected sentinel,
         * so a real critically-low 0% cannot survive that transport. Report a
         * 1% floor over BAS to replace a stale value and keep the half marked
         * connected. Diagnostic output above retains the physical 0% value.
         */
        uint8_t reported_level = MAX(level, 1);
        bt_bas_set_battery_level(reported_level);
    }

    k_work_schedule(&battery_heartbeat_work, BATTERY_HEARTBEAT_INTERVAL);
}

static int battery_heartbeat_init(void) {
    /* The sensor initializes before APPLICATION. Publish a physical reading
     * promptly so the BAS default cannot linger after a reconnect.
     */
    k_work_schedule(&battery_heartbeat_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(battery_heartbeat_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

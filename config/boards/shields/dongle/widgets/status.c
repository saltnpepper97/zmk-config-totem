/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 * 
 * Custom status display for dongle with modifiers, layer, battery, and connection info
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/battery.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct status_widget {
    sys_snode_t node;
    lv_obj_t *obj;
};

struct status_state {
    uint8_t layer;
    uint8_t battery;
    bool usb_connected;
    uint8_t ble_profile;
    bool caps_lock;
    uint8_t modifiers;
};

static struct status_state current_state = {0};

#define MODIFIER_SHIFT 0x01
#define MODIFIER_CTRL  0x02
#define MODIFIER_ALT   0x04
#define MODIFIER_GUI   0x08

static void update_display(lv_obj_t *obj) {
    lv_obj_clean(obj);
    
    char text[256];
    int offset = 0;
    
    // Line 1: Modifiers
    offset += snprintf(text + offset, sizeof(text) - offset, 
                      "%s %s %s %s %s\n",
                      (current_state.modifiers & MODIFIER_SHIFT) ? "SFT" : "   ",
                      (current_state.modifiers & MODIFIER_CTRL)  ? "CTL" : "   ",
                      (current_state.modifiers & MODIFIER_ALT)   ? "ALT" : "   ",
                      (current_state.modifiers & MODIFIER_GUI)   ? "WIN" : "   ",
                      current_state.caps_lock ? "CAP" : "   ");
    
    // Line 2: Layer
    const char *layer_name = zmk_keymap_layer_name(current_state.layer);
    if (layer_name) {
        offset += snprintf(text + offset, sizeof(text) - offset, 
                          "Layer: %s\n", layer_name);
    } else {
        offset += snprintf(text + offset, sizeof(text) - offset, 
                          "Layer: %d\n", current_state.layer);
    }
    
    // Line 3: Battery
    offset += snprintf(text + offset, sizeof(text) - offset, 
                      "Batt: %d%%\n", current_state.battery);
    
    // Line 4: Connection
    if (current_state.usb_connected) {
        offset += snprintf(text + offset, sizeof(text) - offset, "USB");
    } else {
        offset += snprintf(text + offset, sizeof(text) - offset, 
                          "BLE %d", current_state.ble_profile + 1);
    }
    
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
}

static void update_all_widgets(void) {
    struct status_widget *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        update_display(widget->obj);
    }
}

static int status_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer_ev = as_zmk_layer_state_changed(eh);
    if (layer_ev) {
        current_state.layer = zmk_keymap_highest_layer_active();
        update_all_widgets();
        return 0;
    }

    const struct zmk_battery_state_changed *batt_ev = as_zmk_battery_state_changed(eh);
    if (batt_ev) {
        current_state.battery = batt_ev->state_of_charge;
        update_all_widgets();
        return 0;
    }

    const struct zmk_endpoint_changed *ep_ev = as_zmk_endpoint_changed(eh);
    if (ep_ev) {
        struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
        current_state.usb_connected = (endpoint.transport == ZMK_TRANSPORT_USB);
        current_state.ble_profile = endpoint.ble.profile_index;
        update_all_widgets();
        return 0;
    }

    return 0;
}

ZMK_LISTENER(status_widget_listener, status_listener);
ZMK_SUBSCRIPTION(status_widget_listener, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(status_widget_listener, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(status_widget_listener, zmk_endpoint_changed);

int zmk_widget_status_init(struct status_widget *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 128, 64);
    
    sys_slist_append(&widgets, &widget->node);
    
    // Initialize state
    current_state.layer = zmk_keymap_highest_layer_active();
    current_state.battery = zmk_battery_state_of_charge();
    
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    current_state.usb_connected = (endpoint.transport == ZMK_TRANSPORT_USB);
    current_state.ble_profile = endpoint.ble.profile_index;
    
    update_display(widget->obj);
    
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct status_widget *widget) {
    return widget->obj;
}

if(CONFIG_TOTEM_BATTERY_TELEMETRY)
  zephyr_library()
  zephyr_library_sources(src/totem_battery_telemetry.c)
endif()

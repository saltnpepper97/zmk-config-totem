target_sources_ifdef(CONFIG_TOTEM_BATTERY_TELEMETRY app PRIVATE
  src/totem_battery_telemetry.c
)

target_sources_ifdef(CONFIG_TOTEM_BATTERY_HEARTBEAT app PRIVATE
  src/totem_battery_heartbeat.c
)

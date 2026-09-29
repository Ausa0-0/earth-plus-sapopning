# Next Steps

## Before relying on automatic watering

1. Calibrate the capacitive soil sensor in the actual soil. Record the raw ADC value in dry soil and in fully watered soil, then update `SOIL_RAW_DRY` and `SOIL_RAW_WET` in `src/main.cpp`. The current values are starting points, not verified percentages.
2. Verify the threshold behavior at 35% and 60% with the pump disconnected first. Confirm that 36-59% preserves the previous pump state.
3. Check the relay's active level against the hardware. The firmware currently treats GPIO26 HIGH as pump ON. Use a separate, correctly rated pump supply; do not power the pump from an ESP32 GPIO or its 3.3 V output.
4. Test sensor-disconnect behavior. In Auto mode, invalid soil readings should leave the pump OFF; in Manual mode, SW1 toggles the pump while SW2 selects Manual and SW3 selects Auto.

## Network and access

- The supplied Wi-Fi network did not connect during testing. The firmware falls back to the `AutoWater_AP` access point; the web control page has no authentication. Add an AP password and protect web controls before using this outside a supervised lab.
- `WiFi.begin(ssid, password)` supports a conventional Wi-Fi password. Networks requiring WPA2-Enterprise or a captive portal need a different connection setup.
- Keep `include/secrets.h` local. For a fresh checkout, copy `include/secrets.example.h` to `include/secrets.h` and enter local credentials. Do not commit the local file.

## Final validation

- Verify `SOIL_PIN`, `DHT_PIN`, relay, switch, and OLED pins against the assembled PCB, not only the simulation diagram.
- Exercise every button and both modes; confirm OLED and web status agree.
- Recheck raw sensor readings after calibration and document the measured dry/wet values.
- The current workspace has no Git repository or remote configured. Initialize or connect it to the intended GitHub repository only after confirming visibility and reviewing the staged files for secrets.

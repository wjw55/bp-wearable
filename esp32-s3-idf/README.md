# CDE3301 ESP32-S3 ESP-IDF Firmware

This is the first ESP-IDF migration of the Arduino MAX30102 prototype. It keeps the current dashboard contract while preparing the firmware for edge processing and TinyML.

## What this starter does

- Runs on `esp32s3` with ESP-IDF.
- Samples the MAX30102 over I2C using a dedicated FreeRTOS sensor task.
- Filters IR PPG and extracts basic heart-rate and perfusion features in a processing task.
- Estimates trend-based BP from a cuff calibration baseline.
- Posts lightweight JSON to the existing Node backend at `/api/heartrate`.
- Accepts manual cuff calibration over the serial monitor.

## VS Code ESP-IDF setup

1. Install Visual Studio Code and the `Espressif IDF` extension.
2. Open this folder in VS Code:

   ```text
   C:\wjw\NUS_Coursework\Y2S2\CDE3301\Code\CDE3301\esp32-s3-idf
   ```

3. Run `ESP-IDF: Configure ESP-IDF Extension`.
4. Choose an ESP-IDF stable release and let the extension install the tools.
5. Run `ESP-IDF: Set Espressif Device Target`, then choose `esp32s3`.
6. Run `ESP-IDF: SDK Configuration Editor`.
7. Under `CDE3301 Cuffless BP Monitor`, set:
   - `BP_WIFI_SSID`
   - `BP_WIFI_PASSWORD`
   - `BP_SERVER_URL`, for example `http://<your-laptop-ip>:3001/api/heartrate`
   - `BP_I2C_SDA_GPIO` and `BP_I2C_SCL_GPIO` for your ESP32-S3 dev board wiring
8. Build, flash, then monitor from the ESP-IDF extension.

Command-line equivalent after ESP-IDF is installed:

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Use your actual serial port instead of `COMx`.

## Hardware notes

The Arduino prototype used ESP32 pins SDA=21 and SCL=22. ESP32-S3 development boards often use different convenient GPIOs, so this project defaults to SDA=8 and SCL=9 and exposes both in `menuconfig`.

MAX30102 wiring:

```text
MAX30102 VIN  -> 3V3
MAX30102 GND  -> GND
MAX30102 SDA  -> configured SDA GPIO
MAX30102 SCL  -> configured SCL GPIO
```

Keep the I2C wires short. When you move to PCB, keep SDA/SCL away from high-current LED and switching-regulator paths, and use sensible pull-ups near the sensor or MCU side depending on your layout.

## Calibration command

After flashing, open the ESP-IDF serial monitor. Place a finger on the sensor and wait for stable BPM. Then enter:

```text
cal 120 80
```

Replace `120 80` with the cuff reading. The firmware stores the cuff value plus current PPG baseline in NVS and reports whether future readings are calibrated.

The current calibration age is measured from boot or from the last `cal` command. For a strict 8-hour recalibration timer across power cycles, add RTC time from the backend or an external RTC later.

Other monitor commands:

```text
show
help
```

## Current JSON payload

The backend already accepts the original fields:

```json
{
  "bpm": 72,
  "avgBpm": 72,
  "ir": 81000,
  "systolic": 119,
  "diastolic": 78,
  "fingerDetected": true,
  "timestamp": 123456
}
```

This IDF firmware also sends `red`, `filteredIr`, `perfusionIndex`, `calibrated`, and `calibrationAgeSec`. The current Node server ignores unknown fields, so the dashboard should keep working. Later, the backend can persist these extra fields to PostgreSQL for TinyML training.

## Next firmware milestone

Once this builds and reads stable samples on the ESP32-S3 dev board, the next step is to add a data-collection endpoint and save raw/filtered PPG windows plus cuff labels into PostgreSQL.

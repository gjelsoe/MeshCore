#pragma once

#if defined(ESP32_PLATFORM)
// PowerSaving for companions
static void enableCompanionPowerSaving() {
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  // Enable BLE sleep
  esp_bt_sleep_enable();
#endif

  // Configure Power Management
#if CONFIG_IDF_TARGET_ESP32C3
  esp_pm_config_esp32c3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32S3
  esp_pm_config_esp32s3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32
  esp_pm_config_esp32_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32C6
  esp_pm_config_t pm_config;
#endif

#if (defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT) || defined(ENABLE_USB_INTERFACE)
  // Disable automatic light sleep for USB CDC Serial and USB Companion
  pm_config = { .max_freq_mhz = 80, .min_freq_mhz = 40, .light_sleep_enable = false };
#else
  pm_config = { .max_freq_mhz = 80, .min_freq_mhz = 40, .light_sleep_enable = true };
#endif

  esp_err_t errPM = esp_pm_configure(&pm_config);
  if (errPM == ESP_OK) {
    Serial.println("Power Management configured successfully");
  } else {
    Serial.printf("Power Management failed to configure: %d\r\n", errPM);
  }
}

static void disableCompanionPowerSaving() {
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  // Disable BLE sleep
  esp_bt_sleep_disable();
#endif

  // Configure Power Management
#if CONFIG_IDF_TARGET_ESP32C3
  esp_pm_config_esp32c3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32S3
  esp_pm_config_esp32s3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32
  esp_pm_config_esp32_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32C6
  esp_pm_config_t pm_config;
#endif

#if defined(ESP32_CPU_FREQ)
  pm_config = { .max_freq_mhz = ESP32_CPU_FREQ, .min_freq_mhz = ESP32_CPU_FREQ, .light_sleep_enable = false };
#else
  pm_config = { .max_freq_mhz = 80, .min_freq_mhz = 80, .light_sleep_enable = false };
#endif

  esp_err_t errPM = esp_pm_configure(&pm_config);
  if (errPM == ESP_OK) {
    Serial.println("Power Management configured successfully");
  } else {
    Serial.printf("Power Management failed to configure: %d\r\n", errPM);
  }
}

static void applyCompanionPowerSaving(bool powersaving_enabled) {
  if (the_mesh.getNodePrefs()->powersaving_enabled) {
    enableCompanionPowerSaving();
  } else {
    disableCompanionPowerSaving();
  }
}
#endif
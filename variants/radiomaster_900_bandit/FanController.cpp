/**
 * FanController.cpp
 *
 * Implementation of PWM fan control for MeshCore
 */

#include "FanController.h"

// Static variable for tacho interrupt
volatile uint32_t FanController::tachoCounter = 0;

FanController::FanController()
    : currentDuty(0), targetSpeed(0), fanEnabled(false), autoMode(false), lastTachoCount(0), lastTachoTime(0),
      currentRPM(0), tempMin(30.0), tempMax(60.0), nvs_handle(0) {}

bool FanController::begin() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    nvs_flash_erase();
    err = nvs_flash_init();
  }

  // Configure PWM pin
  pinMode(FAN_PWM_PIN, OUTPUT);

  // Setup PWM on ESP32
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
  ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);

  // Start with fan off
  ledcWrite(FAN_PWM_CHANNEL, 0);
  currentDuty = 0;

  // Configure Tacho pin
  pinMode(FAN_TACHO_PIN, INPUT_PULLUP);

  // Attach interrupt to tacho pin (FALLING because tacho pulls to GND)
  attachInterrupt(digitalPinToInterrupt(FAN_TACHO_PIN), tachoISR, FALLING);

  // Reset tacho counter
  tachoCounter = 0;
  lastTachoTime = millis();

  // Load saved settings from NVS
  loadSettings();

  // Apply loaded settings
  if (fanEnabled) {
    currentDuty = percentToDuty(targetSpeed);
    ledcWrite(FAN_PWM_CHANNEL, currentDuty);
  }

  return true;
}

void FanController::update() {
  // Update RPM measurement
  calculateRPM();

  // In auto mode: speed is controlled by temperature updates
  // In manual mode: use target speed directly
  if (!autoMode && fanEnabled) {
    // Update duty cycle if it has changed
    uint8_t newDuty = percentToDuty(targetSpeed);
    if (newDuty != currentDuty) {
      currentDuty = newDuty;
      ledcWrite(FAN_PWM_CHANNEL, currentDuty);
    }
  }
}

void FanController::setSpeed(uint8_t percent) {
  // Limit to 0-100
  if (percent > 100) {
    percent = 100;
  }

  targetSpeed = percent;

  // Update duty cycle immediately
  currentDuty = percentToDuty(percent);
  ledcWrite(FAN_PWM_CHANNEL, currentDuty);

  // Save change to NVS
  saveSettings();
}

uint8_t FanController::getSpeed() const {
  return targetSpeed; // Returner direkte targetSpeed (det vi gemmer i NVS)
}

uint16_t FanController::getRPM() {
  return currentRPM;
}

void FanController::enable(bool enable) {
  fanEnabled = enable;

  if (!enable) {
    // Turn off fan
    currentDuty = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
  } else {
    // Turn on fan at target speed
    currentDuty = percentToDuty(targetSpeed);
    ledcWrite(FAN_PWM_CHANNEL, currentDuty);
  }
}

bool FanController::isEnabled() const {
  return fanEnabled;
}

void FanController::setAutoMode(bool enable) {
  autoMode = enable;
}

bool FanController::isAutoMode() const {
  return autoMode;
}

void FanController::updateFromTemperature(float temperature) {
  if (!autoMode) {
    return;
  }

  uint8_t speed = 0;

  if (temperature <= tempMin) {
    // Below minimum temperature - turn off fan
    speed = 0;
    fanEnabled = false;
  } else if (temperature >= tempMax) {
    // Above maximum temperature - full speed
    speed = 100;
    fanEnabled = true;
  } else {
    // Linear interpolation between min and max
    float range = tempMax - tempMin;
    float offset = temperature - tempMin;
    speed = (uint8_t)((offset / range) * 100.0);
    fanEnabled = true;
  }

  // Update speed
  setSpeed(speed);
}

void FanController::setTempThresholds(float minTemp, float maxTemp) {
  tempMin = minTemp;
  tempMax = maxTemp;
}

void FanController::getDiagnostics(uint16_t &rpm, uint8_t &speed, bool &enabled) {
  rpm = currentRPM;
  speed = targetSpeed; // Brug targetSpeed direkte
  enabled = fanEnabled;
}

void IRAM_ATTR FanController::tachoISR() {
  tachoCounter++;
}

void FanController::calculateRPM() {
  uint32_t currentTime = millis();
  uint32_t elapsed = currentTime - lastTachoTime;

  // Update only once per second
  if (elapsed >= TACHO_SAMPLE_TIME) {
    // Calculate number of pulses in period
    uint32_t pulses = tachoCounter - lastTachoCount;

// Debug output (comment out when working)
#ifdef FAN_DEBUG_RPM
    Serial.printf("[RPM] Pulses: %lu in %lu ms, Total counter: %lu\n", pulses, elapsed, tachoCounter);
#endif

    // Calculate RPM:
    // RPM = (pulses / pulses_per_revolution) * (60000 ms / elapsed ms)
    currentRPM = (pulses * 60000) / (PULSES_PER_REV * elapsed);

#ifdef FAN_DEBUG_RPM
    Serial.printf("[RPM] Calculated: %u RPM\n", currentRPM);
#endif

    // Store values for next calculation
    lastTachoCount = tachoCounter;
    lastTachoTime = currentTime;
  }
}

uint8_t FanController::percentToDuty(uint8_t percent) const {
  if (percent == 0) {
    return FAN_MIN_DUTY;
  }

  // Map from percent (0-100) to duty cycle range
  // We start from FAN_START_DUTY to ensure the fan can start
  if (percent < 30) {
    // Below 30% we use start duty cycle so the fan can start
    return FAN_START_DUTY;
  }

  // Linear mapping from 30-100% to FAN_START_DUTY-FAN_MAX_DUTY
  return map(percent, 30, 100, FAN_START_DUTY, FAN_MAX_DUTY);
}

uint8_t FanController::dutyToPercent(uint8_t duty) const {
  if (duty == 0) {
    return 0;
  }

  if (duty <= FAN_START_DUTY) {
    return 30;
  }

  // Reverse mapping
  return map(duty, FAN_START_DUTY, FAN_MAX_DUTY, 30, 100);
}

bool FanController::saveSettings() {
  // Open NVS in write mode
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    Serial.println("ERROR: Failed to open NVS for writing");
    return false;
  }

  // Save only targetSpeed
  err = nvs_set_u8(nvs_handle, NVS_KEY_SPEED, targetSpeed);
  if (err != ESP_OK) {
    Serial.println("ERROR: Failed to write to NVS");
    nvs_close(nvs_handle);
    return false;
  }

  // Commit changes
  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    Serial.println("ERROR: Failed to commit to NVS");
    nvs_close(nvs_handle);
    return false;
  }

  // Close NVS
  nvs_close(nvs_handle);

  return true;
}

bool FanController::loadSettings() {
  // Open NVS in read-only mode
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    Serial.println("INFO: No saved fan speed found, using default (0)");
    return false;
  }

  // Load targetSpeed (default to 0 if not found)
  uint8_t speed = 0;
  err = nvs_get_u8(nvs_handle, NVS_KEY_SPEED, &speed);
  if (err == ESP_OK) {
    targetSpeed = speed;
    Serial.printf("INFO: Fan speed loaded from NVS: %d%%\n", targetSpeed);
  } else {
    Serial.println("INFO: No saved speed found, using 0%");
    targetSpeed = 0;
  }

  // Close NVS
  nvs_close(nvs_handle);

  return true;
}

bool FanController::resetSettings() {
  // Open NVS in write mode
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    Serial.println("ERROR: Failed to open NVS for reset");
    return false;
  }

  // Delete speed key
  nvs_erase_key(nvs_handle, NVS_KEY_SPEED);

  // Commit changes
  nvs_commit(nvs_handle);

  // Close NVS
  nvs_close(nvs_handle);

  // Reset to default
  targetSpeed = 0;
  currentDuty = 0;
  ledcWrite(FAN_PWM_CHANNEL, 0);

  Serial.println("INFO: Fan speed reset to 0%");

  return true;
}
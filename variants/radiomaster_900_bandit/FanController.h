/**
 * FanController.h
 *
 * PWM-based fan control module for MeshCore
 *
 * Hardware configuration:
 * - PWM pin: GPIO 2
 * - Tacho pin: GPIO 27
 *
 * Features:
 * - PWM speed control (0-100%)
 * - Tacho RPM measurement
 * - Automatic temperature-based control (optional)
 * - Telemetry reporting via MeshCore
 */

#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>

// Hardware pin configuration
#define FAN_PWM_PIN        PA_FAN_EN
#define FAN_TACHO_PIN      PA_FAN_TACHO

// PWM configuration
#define FAN_PWM_FREQ       25000 // 25 kHz PWM frequency (standard for 4-pin fans)
#define FAN_PWM_CHANNEL    0     // ESP32 PWM channel
#define FAN_PWM_RESOLUTION 8     // 8-bit resolution (0-255)

// Fan constants
#define FAN_MIN_DUTY       0   // Minimum duty cycle (0%)
#define FAN_MAX_DUTY       255 // Maximum duty cycle (100%)
#define FAN_START_DUTY     77  // Start duty cycle (~30% - typical minimum for fans)

// Tacho measurement
#define TACHO_SAMPLE_TIME  1000 // Sample time in ms
#define PULSES_PER_REV     2    // Pulses per revolution (standard for PC fans)

// Debug output (uncomment for RPM debugging)
// #define FAN_DEBUG_RPM

// NVS Storage keys
#define NVS_NAMESPACE      "fanctrl" // Namespace for Preferences (max 15 chars)
#define NVS_KEY_SPEED      "speed"   // Key for speed

class FanController {
public:
  FanController();

  /**
   * Initialize the fan controller
   * @return true if initialization succeeded
   */
  bool begin();

  /**
   * Update the fan controller (should be called regularly in loop)
   */
  void update();

  /**
   * Set fan speed in percent (0-100)
   * @param percent Speed in percent (0 = stop, 100 = full speed)
   */
  void setSpeed(uint8_t percent);

  /**
   * Get current fan speed in percent
   * @return Speed in percent (0-100)
   */
  uint8_t getSpeed() const;

  /**
   * Get current RPM from tacho
   * @return RPM value
   */
  uint16_t getRPM();

  /**
   * Enable/disable the fan
   * @param enable true = on, false = off
   */
  void enable(bool enable);

  /**
   * Check if fan is enabled
   * @return true if fan is running
   */
  bool isEnabled() const;

  /**
   * Set automatic temperature-based control
   * @param enable true = automatic, false = manual
   */
  void setAutoMode(bool enable);

  /**
   * Check if automatic mode is active
   * @return true if in automatic mode
   */
  bool isAutoMode() const;

  /**
   * Update fan speed based on temperature (for auto mode)
   * @param temperature Temperature in degrees Celsius
   */
  void updateFromTemperature(float temperature);

  /**
   * Set temperature thresholds for automatic control
   * @param minTemp Temperature where fan starts (°C)
   * @param maxTemp Temperature where fan runs at max (°C)
   */
  void setTempThresholds(float minTemp, float maxTemp);

  /**
   * Get diagnostic information
   * @param rpm Reference to RPM variable
   * @param speed Reference to speed variable (0-100)
   * @param enabled Reference to enabled flag
   */
  void getDiagnostics(uint16_t &rpm, uint8_t &speed, bool &enabled);

  /**
   * Save current settings to NVS (Non-Volatile Storage)
   * @return true if saved successfully
   */
  bool saveSettings();

  /**
   * Load saved settings from NVS
   * @return true if loaded successfully
   */
  bool loadSettings();

  /**
   * Reset saved settings to defaults
   * @return true if reset successfully
   */
  bool resetSettings();

private:
  // Tacho interrupt handler (static)
  static void IRAM_ATTR tachoISR();

  // Calculate RPM from tacho pulses
  void calculateRPM();

  // Map percent to duty cycle
  uint8_t percentToDuty(uint8_t percent) const;

  // Map duty cycle to percent
  uint8_t dutyToPercent(uint8_t duty) const;

  // State variables
  uint8_t currentDuty; // Current duty cycle (0-255)
  uint8_t targetSpeed; // Target speed in percent
  bool fanEnabled;     // Fan status
  bool autoMode;       // Automatic temperature control

  // Tacho measurement
  static volatile uint32_t tachoCounter; // Pulse counter (static for ISR)
  uint32_t lastTachoCount;               // Last tacho count
  uint32_t lastTachoTime;                // Last tacho measurement time
  uint16_t currentRPM;                   // Current RPM

  // Temperature thresholds for auto mode
  float tempMin; // Temperature for minimum speed
  float tempMax; // Temperature for maximum speed

  // NVS storage
  nvs_handle_t nvs_handle; // ESP32 NVS handle for persistent storage
};

#endif // FAN_CONTROLLER_H
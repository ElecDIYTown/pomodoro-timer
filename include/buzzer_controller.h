/*
 * ブザーコントローラ - ポモドーロタイマー v0
 * 圧電ブザーのPWM制御、音量調整
 */

#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

#if defined(ESP32)
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include <esp32-hal-ledc.h>
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif
#endif

// -----------------------------------------------------------------------------
// ブザーコントローラクラス
// -----------------------------------------------------------------------------
class BuzzerController
{
public:
  void begin();
  void setVolumeLevel(uint8_t level);
  uint8_t volumeLevel() const { return volume_level_; }
  void beep(uint32_t duration_ms);
  void update();
  void stop();

private:
  inline void writeDuty(uint32_t duty);

  const uint8_t channel_ = 0;
  const uint32_t base_frequency_ = 2000; // 2 kHz
  const uint8_t resolution_bits_ = 8;
  const uint32_t max_duty_ = (1 << resolution_bits_) - 1;

  uint8_t volume_level_ = 5;
  bool active_ = false;
  uint32_t stop_at_ms_ = 0;
};

#endif // BUZZER_CONTROLLER_H

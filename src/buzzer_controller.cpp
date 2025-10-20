/*
 * ブザーコントローラ実装 - ポモドーロタイマー v0
 */

#include "buzzer_controller.h"

void BuzzerController::begin()
{
#if defined(ESP32) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // Arduino-ESP32 v3.x API
  ledcAttach(PIN_BUZZER, base_frequency_, resolution_bits_);
#else
  // レガシーAPI (v2.x)
  ledcSetup(channel_, base_frequency_, resolution_bits_);
  ledcAttachPin(PIN_BUZZER, channel_);
#endif
  stop();
}

void BuzzerController::setVolumeLevel(uint8_t level)
{
  volume_level_ = constrain(level, static_cast<uint8_t>(0), static_cast<uint8_t>(10));
}

void BuzzerController::beep(uint32_t duration_ms)
{
  if (volume_level_ == 0)
  {
    return;
  }
  uint32_t duty = map(volume_level_, 0, 10, 0, max_duty_);
  writeDuty(duty);
  active_ = true;
  stop_at_ms_ = millis() + duration_ms;
}

void BuzzerController::update()
{
  if (active_ && (millis() >= stop_at_ms_))
  {
    stop();
  }
}

void BuzzerController::stop()
{
  writeDuty(0);
  active_ = false;
}

inline void BuzzerController::writeDuty(uint32_t duty)
{
#if defined(ESP32) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  (void)ledcWrite(PIN_BUZZER, duty);
#else
  ledcWrite(channel_, duty);
#endif
}

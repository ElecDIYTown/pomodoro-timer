/*
 * ESP32-C3 ポモドーロタイマー v0
 * 仕様書参照: pomodoro_timer_spec_v0.md / pomodoro_timer_clarifications_v0.md
 * ボード: Seeed XIAO ESP32-C3
 *
 * 必要なArduinoライブラリ:
 *   - WiFi (ESP32コアにバンドル)
 *   - SPI (バンドル)
 *   - FS / LittleFS (ESP32コアにバンドル)
 *   - SD (ESP32コアにバンドル)
 *   - time (バンドル)
 *   - Adafruit_GFX by Adafruit
 *   - Adafruit_ST7789 by Adafruit
 *   - ArduinoJson by Benoit Blanchon (チェックポイント永続化用)
 */

#include <Arduino.h>
#include "config.h"
#include "pomodoro_timer_app.h"

// -----------------------------------------------------------------------------
// グローバルアプリインスタンス
// -----------------------------------------------------------------------------
static PomodoroTimerApp app;

void setup()
{
  DBG_BEGIN(115200);
  // 少し待ってから開始（USBシリアル安定化のため）
  delay(200);
  DBG_PRINTLN("[BOOT] setup()");
  logWifiCredentialConstants();
  app.begin();
}

void loop()
{
  app.loop();
  // 2秒ごとにステータスをシリアルに出力
  static uint32_t last_log = 0;
  uint32_t now = millis();
  if (now - last_log >= 2000)
  {
    last_log = now;
    DBG_PRINTF("[STAT] state=%d phase=%u rem=%lu ms vol=%u wifi=%d time=%d sd=%d fs=%d\n",
               (int)app.state(),
               (unsigned)app.currentPhaseIndex(),
               (unsigned long)app.remainingMs(),
               (unsigned)app.volumeLevel(),
               (int)app.wifiConnected(),
               (int)app.timeSynchronized(),
               (int)app.sdAvailable(),
               (int)app.littleFsAvailable());
  }
}

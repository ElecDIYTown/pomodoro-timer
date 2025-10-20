/*
 * 設定ファイル - ポモドーロタイマー v0
 * ピン設定、Wi-Fi設定、タイミング定数、フェーズ設定
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// 認証情報オーバーライド (include/secrets.hが存在する場合に読み込まれる)
// -----------------------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID_VALUE
#define WIFI_SSID_VALUE ""
#endif

#ifndef WIFI_PASSWORD_VALUE
#define WIFI_PASSWORD_VALUE ""
#endif

// -----------------------------------------------------------------------------
// デバッグヘルパー
// -----------------------------------------------------------------------------
#ifndef PT_ENABLE_DEBUG
#define PT_ENABLE_DEBUG 1
#endif

#if PT_ENABLE_DEBUG
#define DBG_BEGIN(baud) Serial.begin(baud)
#define DBG_PRINT(x) Serial.print(x)
#define DBG_PRINTLN(x) Serial.println(x)
#define DBG_PRINTF(fmt, ...) Serial.printf((fmt), ##__VA_ARGS__)
#else
#define DBG_BEGIN(baud)
#define DBG_PRINT(x)
#define DBG_PRINTLN(x)
#define DBG_PRINTF(fmt, ...)
#endif

// -----------------------------------------------------------------------------
// レンダリング色 (起動スプラッシュとメインUIで共有)
// -----------------------------------------------------------------------------
#include <Adafruit_ST7789.h>

static const uint16_t COLOR_BACKGROUND = ST77XX_BLACK;
static const uint16_t COLOR_TEXT_PRIMARY = ST77XX_WHITE;
static const uint16_t COLOR_TEXT_DIM = 0x8410; // 約50%グレー
static const uint16_t COLOR_ALERT = ST77XX_RED;
static const uint16_t COLOR_WORK = ST77XX_BLUE;
static const uint16_t COLOR_BREAK = ST77XX_GREEN;
static const uint16_t COLOR_RING_BG = 0x4208; // ダークグレー
static const uint16_t COLOR_BAR_BG = 0x2945;  // ミュートグレーフレーム

// -----------------------------------------------------------------------------
// ピン設定 (Seeed Studio XIAO ESP32-C3)
// -----------------------------------------------------------------------------
constexpr int PIN_SPI_SCK = D8;       // GPIO8
constexpr int PIN_SPI_MOSI = D10;     // GPIO10
constexpr int PIN_SPI_MISO = D9;      // GPIO9
constexpr int PIN_TFT_CS = D6;        // GPIO21
constexpr int PIN_TFT_DC = D2;        // GPIO4
constexpr int PIN_TFT_RST = -1;       // ソフトウェアリセット
constexpr int PIN_SD_CS = D7;         // GPIO20
constexpr int PIN_BUTTON_LADDER = D0; // GPIO2 (ADC)
constexpr int PIN_BUZZER = D1;        // GPIO3 (PWM / LEDC)

// -----------------------------------------------------------------------------
// Wi-Fi / NTP 設定
// -----------------------------------------------------------------------------
extern const char *WIFI_SSID;
extern const char *WIFI_PASSWORD;
extern const char *NTP_SERVER_1;
extern const char *NTP_SERVER_2;
extern const char *NTP_SERVER_3;
constexpr long TIMEZONE_OFFSET_SEC = 9L * 3600L; // JST (+9h)
constexpr int DAYLIGHT_OFFSET_SEC = 0;

#if PT_ENABLE_DEBUG
void logWifiCredentialConstants();
#else
inline void logWifiCredentialConstants() {}
#endif

// -----------------------------------------------------------------------------
// タイミング定数
// -----------------------------------------------------------------------------
constexpr uint32_t TICK_INTERVAL_MS = 10;            // 10 msティック
constexpr uint32_t UI_UPDATE_INTERVAL_MS = 250;      // 250 ms UI更新
constexpr uint32_t CHECKPOINT_INTERVAL_MS = 60000;   // 60 sチェックポイント保存
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;       // 300 msダブルタップウィンドウ
constexpr uint32_t BUTTON_DEBOUNCE_MS = 20;          // 20 msデバウンス
constexpr uint32_t LONG_PRESS_NEXT_MS = 2000;        // 2 s
constexpr uint32_t LONG_PRESS_PREV_MS = 4000;        // 4 s
constexpr uint32_t LONG_PRESS_RESET_MS = 10000;      // 10 s
constexpr uint32_t PRE_ALERT_THRESHOLD_MS = 60000;   // 残り1分
constexpr uint32_t FINAL_COUNTDOWN_WINDOW_MS = 5000; // 最後の5秒

// -----------------------------------------------------------------------------
// サイクル設定
// -----------------------------------------------------------------------------
struct PhaseConfig
{
  uint32_t duration_ms;
  bool is_work;
};

constexpr PhaseConfig PHASES[] = {
    {50UL * 60UL * 1000UL, true},  // 作業 50分
    {5UL * 60UL * 1000UL, false},  // 休憩 5分
    {50UL * 60UL * 1000UL, true},  // 作業 50分
    {10UL * 60UL * 1000UL, false}, // 休憩 10分
};
constexpr size_t PHASE_COUNT = sizeof(PHASES) / sizeof(PHASES[0]);

// ラダー抵抗: 100kΩプルアップ、ボタンは10kΩ / 47kΩ / 68kΩでGNDへ。
// デバイスでの校正後、必要に応じてこれらの中間点を調整する。
constexpr uint16_t ADC_THRESHOLD_BTN1_BTN2 = 840;  // 10kΩと47kΩの中間点
constexpr uint16_t ADC_THRESHOLD_BTN2_BTN3 = 1480; // 47kΩと68kΩの中間点
constexpr uint16_t ADC_THRESHOLD_BTN3_IDLE = 2850; // 68kΩとオープンの中間点

#endif // CONFIG_H

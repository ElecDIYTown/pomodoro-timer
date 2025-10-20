/*
 * ESP32-C3 ポモドーロタイマー v0
 * 仕様参照: pomodoro_timer_spec_v0.md / pomodoro_timer_clarifications_v0.md
 * ボード: Seeed XIAO ESP32-C3
 *
 * 必要なArduinoライブラリ:
 *   - WiFi (ESP32コアに同梱)
 *   - SPI (同梱)
 *   - FS / LittleFS (ESP32コアに同梱)
 *   - SD (ESP32コアに同梱)
 *   - time (同梱)
 *   - Adafruit_GFX by Adafruit
 *   - Adafruit_ST7789 by Adafruit
 *   - ArduinoJson by Benoit Blanchon (チェックポイント永続化用)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
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
// 認証情報のオーバーライド (include/secrets.h が存在する場合にロード)
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
// レンダリングカラー (起動スプラッシュとメインUIで共有)
// -----------------------------------------------------------------------------
static const uint16_t COLOR_BACKGROUND = ST77XX_BLACK;
static const uint16_t COLOR_TEXT_PRIMARY = ST77XX_WHITE;
static const uint16_t COLOR_TEXT_DIM = 0x8410; // 約50%グレー
static const uint16_t COLOR_ALERT = ST77XX_RED;
static const uint16_t COLOR_WORK = ST77XX_BLUE;
static const uint16_t COLOR_BREAK = ST77XX_GREEN;
static const uint16_t COLOR_RING_BG = 0x4208; // ダークグレー
static const uint16_t COLOR_BAR_BG = 0x2945;  // 落ち着いたグレーフレーム

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
// Wi-Fi / NTP設定
// -----------------------------------------------------------------------------
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char *NTP_SERVER_1 = "ntp.nict.jp";
const char *NTP_SERVER_2 = "time.cloudflare.com";
const char *NTP_SERVER_3 = "pool.ntp.org";
constexpr long TIMEZONE_OFFSET_SEC = 9L * 3600L; // JST (日本標準時 +9時間)
constexpr int DAYLIGHT_OFFSET_SEC = 0;

#if PT_ENABLE_DEBUG
static void logWifiCredentialConstants()
{
  DBG_PRINTLN("[DBG] WiFi credential constants:");
  DBG_PRINT("[DBG] WIFI_SSID_VALUE: '");
  DBG_PRINT(WIFI_SSID_VALUE);
  DBG_PRINTLN("'");
  DBG_PRINTF("[DBG] WIFI_SSID_VALUE length: %u\n", static_cast<unsigned>(strlen(WIFI_SSID_VALUE)));
  DBG_PRINTF("[DBG] WIFI_PASSWORD_VALUE length: %u\n", static_cast<unsigned>(strlen(WIFI_PASSWORD_VALUE)));
}
#else
static void logWifiCredentialConstants() {}
#endif

// -----------------------------------------------------------------------------
// タイミング定数
// -----------------------------------------------------------------------------
constexpr uint32_t TICK_INTERVAL_MS = 10;            // 10ミリ秒刻み
constexpr uint32_t UI_UPDATE_INTERVAL_MS = 250;      // 250ミリ秒UI更新
constexpr uint32_t CHECKPOINT_INTERVAL_MS = 60000;   // 60秒チェックポイント保存
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;       // 300ミリ秒ダブルタップウィンドウ
constexpr uint32_t BUTTON_DEBOUNCE_MS = 20;          // 20ミリ秒デバウンス
constexpr uint32_t LONG_PRESS_NEXT_MS = 2000;        // 2秒
constexpr uint32_t LONG_PRESS_PREV_MS = 4000;        // 4秒
constexpr uint32_t LONG_PRESS_RESET_MS = 10000;      // 10秒
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

// -----------------------------------------------------------------------------
// ボタン処理
// -----------------------------------------------------------------------------
enum class ButtonId : uint8_t
{
  NONE = 0,
  BTN1,
  BTN2,
  BTN3
};

enum class ButtonEventType : uint8_t
{
  NONE = 0,
  BTN1_SHORT,
  BTN1_LONG_2S,
  BTN1_LONG_4S,
  BTN1_LONG_10S,
  BTN2_SINGLE,
  BTN2_DOUBLE,
  BTN3_SINGLE,
  BTN3_DOUBLE
};

struct ButtonEvent
{
  ButtonEventType type = ButtonEventType::NONE;
};

// ラダー抵抗: 100kΩプルアップ、ボタンは10kΩ / 47kΩ / 68kΩをGNDに接続
// デバイス上でのキャリブレーション後、必要に応じて中間点を調整
constexpr uint16_t ADC_THRESHOLD_BTN1_BTN2 = 840;  // 10kΩと47kΩの中間点
constexpr uint16_t ADC_THRESHOLD_BTN2_BTN3 = 1480; // 47kΩと68kΩの中間点
constexpr uint16_t ADC_THRESHOLD_BTN3_IDLE = 2850; // 68kΩとオープンの中間点

struct PendingSingle
{
  bool active = false;
  uint32_t timestamp_ms = 0;
};

class PomodoroTimerApp; // コールバック用の前方宣言

class ButtonHandler
{
public:
  void begin()
  {
    analogReadResolution(12);
    pinMode(PIN_BUTTON_LADDER, INPUT);
  }

  void setLongPressObserver(PomodoroTimerApp *observer)
  {
    long_press_observer_ = observer;
  }

  void update(uint32_t now_ms)
  {
    ButtonId raw = decodeButton(analogRead(PIN_BUTTON_LADDER));
    updateDebounce(raw, now_ms);
    finalizePendingSingles(now_ms);
    handleLongPressTick(now_ms);
  }

  bool poll(ButtonEvent &evt)
  {
    if (queue_count_ == 0)
    {
      return false;
    }
    evt = event_queue_[queue_head_];
    queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
    queue_count_--;
    return true;
  }

private:
  static constexpr uint8_t QUEUE_CAPACITY = 8;

  ButtonId decodeButton(uint16_t raw)
  {
    if (raw < ADC_THRESHOLD_BTN1_BTN2)
    {
      return ButtonId::BTN1;
    }
    if (raw < ADC_THRESHOLD_BTN2_BTN3)
    {
      return ButtonId::BTN2;
    }
    if (raw < ADC_THRESHOLD_BTN3_IDLE)
    {
      return ButtonId::BTN3;
    }
    return ButtonId::NONE;
  }

  void updateDebounce(ButtonId raw, uint32_t now_ms)
  {
    if (raw != pending_state_)
    {
      pending_state_ = raw;
      pending_state_since_ms_ = now_ms;
    }

    if (raw == stable_state_)
    {
      return;
    }

    if (now_ms - pending_state_since_ms_ >= BUTTON_DEBOUNCE_MS)
    {
      ButtonId previous = stable_state_;
      stable_state_ = raw;
      if (previous != stable_state_)
      {
        handleStateChange(previous, stable_state_, now_ms);
      }
    }
  }

  void handleStateChange(ButtonId previous, ButtonId current, uint32_t now_ms)
  {
    if (previous == ButtonId::NONE && current != ButtonId::NONE)
    {
      // ボタンが押された
      active_button_ = current;
      press_start_ms_ = now_ms;
      last_long_press_tick_ms_ = now_ms;
    }
    else if (previous != ButtonId::NONE && current == ButtonId::NONE)
    {
      // ボタンが離された
      handleRelease(previous, now_ms);
      active_button_ = ButtonId::NONE;
      last_long_press_tick_ms_ = 0;
    }
  }

  void handleRelease(ButtonId button, uint32_t now_ms)
  {
    uint32_t held_ms = now_ms - press_start_ms_;
    switch (button)
    {
    case ButtonId::BTN1:
      if (held_ms >= LONG_PRESS_RESET_MS)
      {
        enqueue(ButtonEventType::BTN1_LONG_10S);
      }
      else if (held_ms >= LONG_PRESS_PREV_MS)
      {
        enqueue(ButtonEventType::BTN1_LONG_4S);
      }
      else if (held_ms >= LONG_PRESS_NEXT_MS)
      {
        enqueue(ButtonEventType::BTN1_LONG_2S);
      }
      else
      {
        enqueue(ButtonEventType::BTN1_SHORT);
      }
      break;
    case ButtonId::BTN2:
      handleTap(ButtonId::BTN2, held_ms, now_ms);
      break;
    case ButtonId::BTN3:
      handleTap(ButtonId::BTN3, held_ms, now_ms);
      break;
    default:
      break;
    }
  }

  void handleTap(ButtonId button, uint32_t /*held_ms*/, uint32_t now_ms)
  {
    PendingSingle &slot = (button == ButtonId::BTN2) ? pending_btn2_ : pending_btn3_;
    if (slot.active && (now_ms - slot.timestamp_ms) <= DOUBLE_TAP_WINDOW_MS)
    {
      // ダブルタップ
      slot.active = false;
      enqueue(button == ButtonId::BTN2 ? ButtonEventType::BTN2_DOUBLE : ButtonEventType::BTN3_DOUBLE);
    }
    else
    {
      slot.active = true;
      slot.timestamp_ms = now_ms;
      // ウィンドウ経過後に実際のシングルタップが発行される
    }
  }

  void finalizePendingSingles(uint32_t now_ms)
  {
    finalizeSingleForButton(ButtonId::BTN2, pending_btn2_, now_ms);
    finalizeSingleForButton(ButtonId::BTN3, pending_btn3_, now_ms);
  }

  void finalizeSingleForButton(ButtonId button, PendingSingle &slot, uint32_t now_ms)
  {
    if (!slot.active)
    {
      return;
    }
    if ((now_ms - slot.timestamp_ms) > DOUBLE_TAP_WINDOW_MS)
    {
      slot.active = false;
      enqueue(button == ButtonId::BTN2 ? ButtonEventType::BTN2_SINGLE : ButtonEventType::BTN3_SINGLE);
    }
  }

  void enqueue(ButtonEventType type)
  {
    if (type == ButtonEventType::NONE)
    {
      return;
    }
    if (queue_count_ >= QUEUE_CAPACITY)
    {
      // 最新のインタラクションを応答性良く保つため、最も古いものを削除
      queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
      queue_count_--;
    }
    uint8_t index = (queue_head_ + queue_count_) % QUEUE_CAPACITY;
    event_queue_[index].type = type;
    queue_count_++;
  }

  void handleLongPressTick(uint32_t now_ms);

  ButtonId stable_state_ = ButtonId::NONE;
  ButtonId pending_state_ = ButtonId::NONE;
  uint32_t pending_state_since_ms_ = 0;
  ButtonId active_button_ = ButtonId::NONE;
  uint32_t press_start_ms_ = 0;
  uint32_t last_long_press_tick_ms_ = 0;

  PendingSingle pending_btn2_;
  PendingSingle pending_btn3_;

  ButtonEvent event_queue_[QUEUE_CAPACITY];
  uint8_t queue_head_ = 0;
  uint8_t queue_count_ = 0;

  PomodoroTimerApp *long_press_observer_ = nullptr;
};

// -----------------------------------------------------------------------------
// オーディオ（ブザー）制御
// -----------------------------------------------------------------------------
class BuzzerController
{
public:
  void begin()
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

  void setVolumeLevel(uint8_t level)
  {
    volume_level_ = constrain(level, static_cast<uint8_t>(0), static_cast<uint8_t>(10));
  }

  uint8_t volumeLevel() const { return volume_level_; }

  void beep(uint32_t duration_ms)
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

  void update()
  {
    if (active_ && (millis() >= stop_at_ms_))
    {
      stop();
    }
  }

  void stop()
  {
    writeDuty(0);
    active_ = false;
  }

private:
  inline void writeDuty(uint32_t duty)
  {
#if defined(ESP32) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    (void)ledcWrite(PIN_BUZZER, duty);
#else
    ledcWrite(channel_, duty);
#endif
  }

  const uint8_t channel_ = 0;
  const uint32_t base_frequency_ = 2000; // 2kHz
  const uint8_t resolution_bits_ = 8;
  const uint32_t max_duty_ = (1 << resolution_bits_) - 1;

  uint8_t volume_level_ = 5;
  bool active_ = false;
  uint32_t stop_at_ms_ = 0;
};

// -----------------------------------------------------------------------------
// 前方宣言
// -----------------------------------------------------------------------------
class PomodoroTimerApp;
static void renderUI(PomodoroTimerApp &app);
static void drawProgressBar(Adafruit_ST7789 &display, float remaining_ratio);
static void drawWifiIndicator(Adafruit_ST7789 &display, int16_t x, int16_t y, bool connected);
static void drawCenteredText(Adafruit_ST7789 &display, const char *text, int16_t y, uint8_t size, uint16_t color);

// -----------------------------------------------------------------------------
// ポモドーロタイマーコア
// -----------------------------------------------------------------------------
enum class TimerState : uint8_t
{
  STOPPED = 0,
  RUNNING,
  PAUSED
};

enum class SegmentEndReason : uint8_t
{
  COMPLETED,
  PAUSED,
  RESET,
  SKIPPED,
  PREV,
  DAY_CUTOVER,
  RECOVERED_DROP
};

static const char *const SEGMENT_REASON_STRINGS[] = {
    "completed",     // 完了
    "paused",        // 一時停止
    "reset",         // リセット
    "skipped",       // スキップ
    "prev",          // 前へ
    "day_cutover",   // 日付切替
    "recovered_drop" // ドロップ回復
};

struct SegmentMetadata
{
  uint16_t session_id = 1;
  uint16_t segment_id = 1;
  bool is_work = true;
  char cycle_label[16] = "Work#1";
  time_t start_epoch = 0;
};

struct CheckpointPayload
{
  uint8_t version = 1;
  time_t timestamp_saved = 0;
  String current_state; // "running" / "paused" / "stopped"
  uint16_t session_id = 1;
  uint16_t segment_id = 1;
  String cycle_label;
  time_t segment_start_epoch = 0;
  uint32_t elapsed_ms_in_segment = 0;
  uint32_t remaining_ms = 0;
  uint8_t volume_level = 5;
  time_t last_sync_epoch = 0;
  int32_t clock_offset_ms = 0;
  uint8_t phase_index = 0;
  uint16_t work_count = 0;
  uint16_t break_count = 0;
  uint32_t today_focus_ms = 0;
  String current_date_key;
};

class PomodoroTimerApp
{
public:
  void begin()
  {
    DBG_PRINTLN("[BOOT] PomodoroTimerApp.begin()");
    button_handler_.begin();
    button_handler_.setLongPressObserver(this);
    buzzer_.begin();
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    DBG_PRINTF("[BOOT] SPI.begin SCK=%d MOSI=%d MISO=%d\n", PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SPI_MISO);

    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);

    initDisplay();
    splash("Pomodoro v0", "Booting...", ST77XX_WHITE);

    initStorage();
    initWiFiAndTime();
    drawBootBadges();

    loadCheckpointOrReset();
    last_ui_render_ms_ = millis();
    checkpoint_next_due_ms_ = millis() + CHECKPOINT_INTERVAL_MS;
    last_diag_log_ms_ = millis();
    DBG_PRINTLN("[BOOT] begin() done");
  }

  void loop()
  {
    uint32_t now_ms = millis();
    button_handler_.update(now_ms);
    processButtonEvents();

    if (now_ms - last_tick_ms_ >= TICK_INTERVAL_MS)
    {
      uint32_t delta = now_ms - last_tick_ms_;
      last_tick_ms_ = now_ms;
      updateTimer(delta);
      buzzer_.update();
      maybeHandleDayCutover();
      maybeSaveCheckpoint(now_ms);
    }

    if (now_ms - last_ui_render_ms_ >= UI_UPDATE_INTERVAL_MS)
    {
      renderUI(*this);
      last_ui_render_ms_ = now_ms;
    }
  }

  // ---------------------------------------------------------------------------
  // UI / ステータス用アクセサ
  // ---------------------------------------------------------------------------
  Adafruit_ST7789 &display() { return tft_; }
  TimerState state() const { return state_; }
  const SegmentMetadata &segment() const { return current_segment_; }
  uint8_t currentPhaseIndex() const { return current_phase_index_; }
  uint32_t remainingMs() const { return remaining_ms_; }
  uint32_t basePhaseDurationMs() const { return PHASES[current_phase_index_].duration_ms; }
  bool isWorkPhase() const { return PHASES[current_phase_index_].is_work; }
  uint32_t todayFocusMs() const { return today_focus_ms_; }
  uint8_t volumeLevel() const { return buzzer_.volumeLevel(); }
  bool wifiConnected() const { return wifi_connected_; }
  bool timeSynchronized() const { return time_synced_; }
  bool sdAvailable() const { return sd_available_; }
  bool littleFsAvailable() const { return littlefs_available_; }
  const char *cycleLabel() const { return current_segment_.cycle_label; }
  const char *dateKey() const { return current_date_key_; }

  void onButtonLongPressTick(ButtonId button, uint32_t seconds_elapsed)
  {
    if (button != ButtonId::BTN1)
    {
      return;
    }
    if (seconds_elapsed == 0)
    {
      return;
    }
    DBG_PRINTF("[BTN] BTN1 long press %lu s\n", static_cast<unsigned long>(seconds_elapsed));
    buzzer_.beep(60);
  }

  uint32_t elapsedMsInSegment() const { return elapsed_ms_in_segment_; }
  time_t lastSyncEpoch() const { return last_sync_epoch_; }

private:
  // ---------------------------------------------------------------------------
  // 初期化ヘルパー
  // ---------------------------------------------------------------------------
  void initDisplay()
  {
    DBG_PRINTLN("[BOOT] initDisplay: ST7789 init start");
    tft_ = Adafruit_ST7789(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
    tft_.init(240, 240);
    tft_.setRotation(2); // USBコネクタが下
    tft_.fillScreen(ST77XX_BLACK);
    tft_.setTextWrap(false);
    // 視覚的確認用のシンプルなボーダー
    tft_.fillRect(0, 0, 240, 6, ST77XX_BLUE);
    tft_.fillRect(0, 234, 240, 6, ST77XX_GREEN);
    DBG_PRINTLN("[BOOT] initDisplay: OK");
  }

  void initStorage()
  {
    DBG_PRINTLN("[BOOT] initStorage: LittleFS.begin");
    littlefs_available_ = LittleFS.begin(true);
    if (littlefs_available_)
    {
      LittleFS.mkdir("/system");
      DBG_PRINTLN("[FS] LittleFS OK");
    }
    else
    {
      DBG_PRINTLN("[FS] LittleFS FAIL");
    }

    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_SD_CS, HIGH);
    DBG_PRINTLN("[BOOT] initStorage: SD.begin");
    sd_available_ = SD.begin(PIN_SD_CS, SPI, 20000000);
    if (sd_available_)
    {
      if (!SD.exists("/logs"))
      {
        SD.mkdir("/logs");
      }
      DBG_PRINTLN("[SD] SD card OK");
    }
    else
    {
      DBG_PRINTLN("[SD] SD card FAIL");
      beepError(1);
    }
  }

  void initWiFiAndTime()
  {
    DBG_PRINTLN("[BOOT] initWiFiAndTime: WiFi begin");
    const char *ssid_ptr = WIFI_SSID;
    const char *pass_ptr = WIFI_PASSWORD;
    size_t ssid_len = ssid_ptr ? strlen(ssid_ptr) : 0U;
    size_t pass_len = pass_ptr ? strlen(pass_ptr) : 0U;

    if (!ssid_ptr)
    {
      DBG_PRINTLN("[WiFi] ERROR: WIFI_SSID pointer is null");
    }
    else
    {
      DBG_PRINT("[WiFi] SSID: '");
      DBG_PRINT(ssid_ptr);
      DBG_PRINTLN("'");
    }
    DBG_PRINTF("[WiFi] SSID length: %u\n", static_cast<unsigned>(ssid_len));
    DBG_PRINTF("[WiFi] Password length: %u\n", static_cast<unsigned>(pass_len));

    if (ssid_len == 0)
    {
      DBG_PRINTLN("[WiFi] ERROR: SSID is empty!");
    }
    if (pass_len == 0)
    {
      DBG_PRINTLN("[WiFi] ERROR: Password is empty!");
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    wifi_connected_ = false;
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000)
    {
      delay(250);
      DBG_PRINT(".");
    }
    DBG_PRINTLN("");
    wifi_connected_ = (WiFi.status() == WL_CONNECTED);

    // WiFi接続状況の詳細ログ出力
    wl_status_t status = WiFi.status();
    DBG_PRINTF("[WiFi] Status code: %d\n", status);
    switch (status)
    {
    case WL_CONNECTED:
      DBG_PRINTLN("[WiFi] Status: Connected");
      DBG_PRINTF("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
      DBG_PRINTF("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
      break;
    case WL_NO_SSID_AVAIL:
      DBG_PRINTLN("[WiFi] Status: SSID not available");
      break;
    case WL_CONNECT_FAILED:
      DBG_PRINTLN("[WiFi] Status: Connection failed");
      break;
    case WL_CONNECTION_LOST:
      DBG_PRINTLN("[WiFi] Status: Connection lost");
      break;
    case WL_DISCONNECTED:
      DBG_PRINTLN("[WiFi] Status: Disconnected");
      break;
    default:
      DBG_PRINTF("[WiFi] Status: Unknown (%d)\n", status);
      break;
    }

    DBG_PRINTLN(wifi_connected_ ? "[WiFi] Connected" : "[WiFi] Not connected");

    configTime(TIMEZONE_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    setenv("TZ", "JST-9", 1);
    tzset();

    if (wifi_connected_)
    {
      struct tm timeinfo;
      if (waitForTimeSync(timeinfo))
      {
        time_synced_ = true;
        last_sync_epoch_ = time(nullptr);
        last_ntp_sync_ms_ = millis();
        DBG_PRINTLN("[TIME] NTP sync OK");
        uint8_t previous_volume = buzzer_.volumeLevel();
        buzzer_.setVolumeLevel(1);
        beepOk(1);
        buzzer_.setVolumeLevel(previous_volume);
      }
      else
      {
        DBG_PRINTLN("[TIME] NTP sync FAIL");
        beepError(2, 1);
      }
    }
    else
    {
      time_synced_ = false;
      beepError(2, 1);
    }
  }

  bool waitForTimeSync(struct tm &info)
  {
    uint32_t start = millis();
    while ((millis() - start) < 5000)
    {
      if (getLocalTime(&info))
      {
        return true;
      }
      delay(50);
    }
    return false;
  }

  void loadCheckpointOrReset()
  {
    if (!littlefs_available_)
    {
      resetState();
      return;
    }
    if (!LittleFS.exists("/system/checkpoint.json"))
    {
      resetState();
      return;
    }

    File file = LittleFS.open("/system/checkpoint.json", "r");
    if (!file)
    {
      resetState();
      return;
    }

    StaticJsonDocument<768> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err)
    {
      resetState();
      return;
    }

    CheckpointPayload payload;
    payload.version = doc["version"] | 1;
    payload.timestamp_saved = doc["timestamp_saved"] | 0;
    payload.current_state = (const char *)(doc["current_state"] | "stopped");
    payload.session_id = doc["session_id"] | 1;
    payload.segment_id = doc["segment_id"] | 1;
    payload.cycle_label = (const char *)(doc["cycle_label"] | "Work#1");
    payload.segment_start_epoch = doc["segment_start_epoch"] | 0;
    payload.elapsed_ms_in_segment = doc["elapsed_ms_in_segment"] | 0;
    payload.remaining_ms = doc["remaining_ms"] | PHASES[0].duration_ms;
    payload.volume_level = doc["volume_level"] | 5;
    payload.last_sync_epoch = doc["last_sync_epoch"] | 0;
    payload.clock_offset_ms = doc["clock_offset_ms"] | 0;
    payload.phase_index = doc["phase_index"] | 0;
    payload.work_count = doc["work_count"] | 0;
    payload.break_count = doc["break_count"] | 0;
    payload.today_focus_ms = doc["today_focus_ms"] | 0;
    payload.current_date_key = (const char *)(doc["current_date_key"] | "");

    restoreFromCheckpoint(payload);
  }

  void restoreFromCheckpoint(const CheckpointPayload &payload)
  {
    buzzer_.setVolumeLevel(payload.volume_level);
    current_phase_index_ = constrain(payload.phase_index, static_cast<uint8_t>(0), static_cast<uint8_t>(PHASE_COUNT - 1));
    strncpy(current_segment_.cycle_label, payload.cycle_label.c_str(), sizeof(current_segment_.cycle_label));
    current_segment_.cycle_label[sizeof(current_segment_.cycle_label) - 1] = '\0';
    current_segment_.session_id = payload.session_id == 0 ? 1 : payload.session_id;
    current_segment_.segment_id = payload.segment_id == 0 ? 1 : payload.segment_id;
    current_segment_.is_work = PHASES[current_phase_index_].is_work;
    current_segment_.start_epoch = payload.segment_start_epoch;
    remaining_ms_ = payload.remaining_ms;
    elapsed_ms_in_segment_ = payload.elapsed_ms_in_segment;
    work_cycle_count_ = payload.work_count;
    break_cycle_count_ = payload.break_count;
    today_focus_ms_ = payload.today_focus_ms;
    strncpy(current_date_key_, payload.current_date_key.c_str(), sizeof(current_date_key_));
    current_date_key_[sizeof(current_date_key_) - 1] = '\0';
    last_sync_epoch_ = payload.last_sync_epoch;
    next_session_id_ = current_segment_.session_id + 1;
    pre_alert_triggered_ = (remaining_ms_ <= PRE_ALERT_THRESHOLD_MS && remaining_ms_ > 0);
    final_countdown_last_sec_ = 0;

    if (!current_date_key_[0])
    {
      updateCurrentDateKey();
    }

    if (payload.current_state == "running")
    {
      // 回復されたドロップとして扱う
      logRecoveredDrop(payload);
      state_ = TimerState::PAUSED;
      // 再開用にセグメントIDをインクリメント
      current_segment_.segment_id += 1;
      elapsed_ms_in_segment_ = 0;
    }
    else if (payload.current_state == "paused")
    {
      state_ = TimerState::PAUSED;
    }
    else
    {
      state_ = TimerState::STOPPED;
    }
  }

  void logRecoveredDrop(const CheckpointPayload &payload)
  {
    if (!sd_available_)
    {
      return;
    }
    if (payload.segment_start_epoch == 0 || payload.timestamp_saved == 0)
    {
      return;
    }
    uint32_t duration_ms = payload.elapsed_ms_in_segment;
    if (duration_ms == 0 && payload.timestamp_saved > payload.segment_start_epoch)
    {
      duration_ms = static_cast<uint32_t>(payload.timestamp_saved - payload.segment_start_epoch) * 1000UL;
    }
    appendCsvEntry(payload.session_id,
                   payload.segment_id,
                   payload.segment_start_epoch,
                   payload.timestamp_saved,
                   duration_ms,
                   SegmentEndReason::RECOVERED_DROP,
                   payload.volume_level,
                   payload.cycle_label.c_str());
  }

  void resetState()
  {
    state_ = TimerState::STOPPED;
    current_phase_index_ = 0;
    remaining_ms_ = PHASES[0].duration_ms;
    elapsed_ms_in_segment_ = 0;
    current_segment_.session_id = 1;
    current_segment_.segment_id = 1;
    current_segment_.is_work = PHASES[0].is_work;
    strncpy(current_segment_.cycle_label, "Work#1", sizeof(current_segment_.cycle_label));
    current_segment_.start_epoch = 0;
    work_cycle_count_ = 0;
    break_cycle_count_ = 0;
    today_focus_ms_ = 0;
    next_session_id_ = 1;
    updateCurrentDateKey();
    buzzer_.setVolumeLevel(5);
  }

  // ---------------------------------------------------------------------------
  // ボタン処理
  // ---------------------------------------------------------------------------
  void processButtonEvents()
  {
    ButtonEvent evt;
    while (button_handler_.poll(evt))
    {
      switch (evt.type)
      {
      case ButtonEventType::BTN1_SHORT:
        handleStartPause();
        break;
      case ButtonEventType::BTN1_LONG_2S:
        handleSkip();
        break;
      case ButtonEventType::BTN1_LONG_4S:
        handlePrev();
        break;
      case ButtonEventType::BTN1_LONG_10S:
        handleFactoryReset();
        break;
      case ButtonEventType::BTN2_SINGLE:
        adjustRemainingMinutes(-1);
        break;
      case ButtonEventType::BTN2_DOUBLE:
        adjustVolume(-1);
        break;
      case ButtonEventType::BTN3_SINGLE:
        adjustRemainingMinutes(+1);
        break;
      case ButtonEventType::BTN3_DOUBLE:
        adjustVolume(+1);
        break;
      default:
        break;
      }
    }
  }

  void handleStartPause()
  {
    if (state_ == TimerState::STOPPED)
    {
      beginPhase(current_phase_index_);
      state_ = TimerState::RUNNING;
    }
    else if (state_ == TimerState::RUNNING)
    {
      // 一時停止
      state_ = TimerState::PAUSED;
      finalizeSegment(SegmentEndReason::PAUSED);
    }
    else if (state_ == TimerState::PAUSED)
    {
      resumeCurrentPhase();
    }
  }

  void handleSkip()
  {
    if (state_ == TimerState::STOPPED)
    {
      advancePhase(+1, false);
      return;
    }

    SegmentEndReason reason = (state_ == TimerState::RUNNING) ? SegmentEndReason::SKIPPED : SegmentEndReason::SKIPPED;
    finalizeSegment(reason);
    advancePhase(+1, true);
  }

  void handlePrev()
  {
    if (state_ == TimerState::STOPPED)
    {
      advancePhase(-1, false);
      return;
    }

    finalizeSegment(SegmentEndReason::PREV);
    advancePhase(-1, true);
  }

  void handleFactoryReset()
  {
    DBG_PRINTLN("[BTN] Factory reset requested");

    bool checkpoint_removed = false;
    if (littlefs_available_)
    {
      if (LittleFS.exists("/system/checkpoint.json"))
      {
        checkpoint_removed = LittleFS.remove("/system/checkpoint.json");
        DBG_PRINTLN(checkpoint_removed ? "[RESET] checkpoint removed" : "[RESET] checkpoint remove failed");
      }
      else
      {
        DBG_PRINTLN("[RESET] checkpoint not present");
      }
    }
    else
    {
      DBG_PRINTLN("[RESET] LittleFS unavailable");
    }

    resetState();

    uint32_t now_ms = millis();
    last_tick_ms_ = now_ms;
    last_ui_render_ms_ = now_ms - UI_UPDATE_INTERVAL_MS; // force refresh on next loop
    checkpoint_next_due_ms_ = now_ms + CHECKPOINT_INTERVAL_MS;

    beepOk(2);
  }

  void adjustRemainingMinutes(int8_t delta_minutes)
  {
    int32_t new_remaining = static_cast<int32_t>(remaining_ms_) + static_cast<int32_t>(delta_minutes) * 60000L;
    if (new_remaining < 0)
    {
      new_remaining = 0;
    }
    remaining_ms_ = static_cast<uint32_t>(new_remaining);
    if (state_ == TimerState::RUNNING)
    {
      uint32_t base_duration = PHASES[current_phase_index_].duration_ms;
      elapsed_ms_in_segment_ = base_duration > remaining_ms_ ? base_duration - remaining_ms_ : 0;
    }
  }

  void adjustVolume(int8_t delta)
  {
    int16_t level = static_cast<int16_t>(buzzer_.volumeLevel()) + delta;
    level = constrain(level, static_cast<int16_t>(0), static_cast<int16_t>(10));
    buzzer_.setVolumeLevel(level);
  }

  // ---------------------------------------------------------------------------
  // フェーズ / セグメント制御
  // ---------------------------------------------------------------------------
  void beginPhase(uint8_t phase_index)
  {
    current_phase_index_ = phase_index % PHASE_COUNT;
    bool is_work = PHASES[current_phase_index_].is_work;

    if (is_work)
    {
      work_cycle_count_ += 1;
      snprintf(current_segment_.cycle_label, sizeof(current_segment_.cycle_label), "Work#%u", work_cycle_count_);
      // 新しい作業セッション => セッションIDをインクリメントしセグメントIDをリセット
      incrementSessionId();
      current_segment_.segment_id = 1;
    }
    else
    {
      break_cycle_count_ += 1;
      snprintf(current_segment_.cycle_label, sizeof(current_segment_.cycle_label), "Break#%u", break_cycle_count_);
      incrementSessionId();
      current_segment_.segment_id = 1;
    }

    current_segment_.is_work = is_work;
    remaining_ms_ = PHASES[current_phase_index_].duration_ms;
    elapsed_ms_in_segment_ = 0;
    current_segment_.start_epoch = determineSegmentStartEpoch();
    pre_alert_triggered_ = false;
    final_countdown_last_sec_ = 0;
  }

  void resumeCurrentPhase()
  {
    state_ = TimerState::RUNNING;
    current_segment_.segment_id += 1;
    current_segment_.start_epoch = determineSegmentStartEpoch();
    elapsed_ms_in_segment_ = 0;
  }

  void advancePhase(int direction, bool auto_start)
  {
    if (direction > 0)
    {
      if (current_phase_index_ >= PHASE_COUNT - 1)
      {
        concludeCycleAndHold();
        return;
      }
      current_phase_index_ += 1;
    }
    else if (direction < 0)
    {
      if (current_phase_index_ == 0)
      {
        current_phase_index_ = PHASE_COUNT - 1;
      }
      else
      {
        current_phase_index_ -= 1;
      }
    }

    if (auto_start)
    {
      state_ = TimerState::RUNNING;
      beginPhase(current_phase_index_);
    }
    else
    {
      state_ = TimerState::STOPPED;
      remaining_ms_ = PHASES[current_phase_index_].duration_ms;
      elapsed_ms_in_segment_ = 0;
    }
  }

  // 設定されたサイクルが終了したら、タイマーの状態をリセット
  void concludeCycleAndHold()
  {
    state_ = TimerState::STOPPED;
    pre_alert_triggered_ = false;
    final_countdown_last_sec_ = 0;
    work_cycle_count_ = 0;
    break_cycle_count_ = 0;
    current_phase_index_ = 0;
    remaining_ms_ = PHASES[0].duration_ms;
    elapsed_ms_in_segment_ = 0;
    current_segment_.is_work = PHASES[0].is_work;
    current_segment_.segment_id = 1;
    current_segment_.session_id = next_session_id_;
    strncpy(current_segment_.cycle_label, "Work#1", sizeof(current_segment_.cycle_label));
    current_segment_.cycle_label[sizeof(current_segment_.cycle_label) - 1] = '\0';
    current_segment_.start_epoch = 0;
  }

  void updateTimer(uint32_t delta_ms)
  {
    if (state_ != TimerState::RUNNING)
    {
      return;
    }

    if (remaining_ms_ > delta_ms)
    {
      remaining_ms_ -= delta_ms;
      elapsed_ms_in_segment_ += delta_ms;
    }
    else
    {
      elapsed_ms_in_segment_ += remaining_ms_;
      remaining_ms_ = 0;
    }

    triggerAlerts();

    if (remaining_ms_ == 0)
    {
      bool last_phase = (current_phase_index_ == PHASE_COUNT - 1);
      finalizeSegment(SegmentEndReason::COMPLETED);
      if (last_phase)
      {
        concludeCycleAndHold();
      }
      else
      {
        advancePhase(+1, true);
      }
    }
  }

  void triggerAlerts()
  {
    if (remaining_ms_ <= PRE_ALERT_THRESHOLD_MS && !pre_alert_triggered_)
    {
      buzzer_.beep(300);
      pre_alert_triggered_ = true;
    }

    if (remaining_ms_ <= FINAL_COUNTDOWN_WINDOW_MS)
    {
      uint32_t secs_left = (remaining_ms_ + 999) / 1000;
      if (secs_left <= 5 && secs_left > 0 && final_countdown_last_sec_ != secs_left)
      {
        final_countdown_last_sec_ = secs_left;
        buzzer_.beep(150);
      }
    }
    else
    {
      final_countdown_last_sec_ = 0;
    }
  }

  void finalizeSegment(SegmentEndReason reason)
  {
    time_t end_epoch = determineSegmentEndEpoch();

    uint32_t base_duration = PHASES[current_phase_index_].duration_ms;
    uint32_t spent_ms = base_duration > remaining_ms_ ? base_duration - remaining_ms_ : elapsed_ms_in_segment_;

    if (reason == SegmentEndReason::PAUSED)
    {
      // 状態を変更しない (既に一時停止中)
    }
    else
    {
      // 実行中状態を停止 (呼び出し元で設定される)
      state_ = TimerState::PAUSED;
    }

    if (current_segment_.is_work)
    {
      today_focus_ms_ += spent_ms;
    }

    if (sd_available_)
    {
      appendCsvEntry(current_segment_.session_id,
                     current_segment_.segment_id,
                     current_segment_.start_epoch,
                     end_epoch,
                     spent_ms,
                     reason,
                     buzzer_.volumeLevel(),
                     current_segment_.cycle_label);
    }
  }

  time_t determineSegmentStartEpoch()
  {
    time_t now_epoch = time(nullptr);
    if (now_epoch < 100000)
    {
      // 最後の同期エポックまたはゼロにフォールバック
      if (last_sync_epoch_ > 0)
      {
        now_epoch = last_sync_epoch_ + ((millis() - last_ntp_sync_ms_) / 1000);
      }
    }
    return now_epoch;
  }

  time_t determineSegmentEndEpoch()
  {
    time_t now_epoch = time(nullptr);
    if (now_epoch < 100000)
    {
      if (last_sync_epoch_ > 0)
      {
        now_epoch = last_sync_epoch_ + ((millis() - last_ntp_sync_ms_) / 1000);
      }
      else if (current_segment_.start_epoch > 0)
      {
        now_epoch = current_segment_.start_epoch + (elapsed_ms_in_segment_ / 1000);
      }
    }
    return now_epoch;
  }

  void incrementSessionId()
  {
    // 日付が変わったらセッションカウンタをリセット
    ensureDateKeyFresh();
    current_segment_.session_id = next_session_id_;
    next_session_id_ += 1;
  }

  void ensureDateKeyFresh()
  {
    char today_key[9] = {0};
    buildCurrentDateKey(today_key, sizeof(today_key));
    if (strcmp(today_key, current_date_key_) != 0)
    {
      strncpy(current_date_key_, today_key, sizeof(current_date_key_));
      current_date_key_[sizeof(current_date_key_) - 1] = '\0';
      next_session_id_ = 1;
      today_focus_ms_ = 0;
    }
  }

  void updateCurrentDateKey()
  {
    buildCurrentDateKey(current_date_key_, sizeof(current_date_key_));
    current_date_key_[sizeof(current_date_key_) - 1] = '\0';
  }

  void buildCurrentDateKey(char *out, size_t size)
  {
    struct tm info;
    if (getLocalTime(&info, 50))
    {
      snprintf(out, size, "%04d%02d%02d", info.tm_year + 1900, info.tm_mon + 1, info.tm_mday);
    }
    else
    {
      snprintf(out, size, "19700101");
    }
  }

  void maybeHandleDayCutover()
  {
    if (state_ != TimerState::RUNNING)
    {
      ensureDateKeyFresh();
      return;
    }

    char today_key[9] = {0};
    buildCurrentDateKey(today_key, sizeof(today_key));
    if (strcmp(today_key, current_date_key_) == 0)
    {
      return;
    }

    // 日付が変わった -> 現在のセグメントを真夜中に確定
    time_t now_epoch = time(nullptr);
    struct tm info;
    if (!getLocalTime(&info, 50))
    {
      ensureDateKeyFresh();
      return;
    }

    // 新しい日の真夜中エポックを計算
    struct tm midnight_info = info;
    midnight_info.tm_hour = 0;
    midnight_info.tm_min = 0;
    midnight_info.tm_sec = 0;
    time_t midnight_epoch = mktime(&midnight_info);

    uint32_t duration_ms = 0;
    if (midnight_epoch > current_segment_.start_epoch)
    {
      duration_ms = static_cast<uint32_t>(midnight_epoch - current_segment_.start_epoch) * 1000UL;
    }

    if (current_segment_.is_work)
    {
      today_focus_ms_ += duration_ms;
    }

    if (sd_available_)
    {
      appendCsvEntry(current_segment_.session_id,
                     current_segment_.segment_id,
                     current_segment_.start_epoch,
                     midnight_epoch,
                     duration_ms,
                     SegmentEndReason::DAY_CUTOVER,
                     buzzer_.volumeLevel(),
                     current_segment_.cycle_label);
    }

    // 新しい日のセッションを準備
    strncpy(current_date_key_, today_key, sizeof(current_date_key_));
    next_session_id_ = 1;
    today_focus_ms_ = 0;
    current_segment_.session_id = next_session_id_++;
    current_segment_.segment_id = 1;
    current_segment_.start_epoch = midnight_epoch;
    // remaining_ms_は既に残り時間を反映（一部消費済みのため）
    elapsed_ms_in_segment_ = 0;
    pre_alert_triggered_ = (remaining_ms_ <= PRE_ALERT_THRESHOLD_MS);
    final_countdown_last_sec_ = 0;
  }

  void maybeSaveCheckpoint(uint32_t now_ms)
  {
    if (!littlefs_available_)
    {
      return;
    }
    if (now_ms < checkpoint_next_due_ms_)
    {
      return;
    }
    checkpoint_next_due_ms_ = now_ms + CHECKPOINT_INTERVAL_MS;

    CheckpointPayload payload;
    payload.version = 1;
    payload.timestamp_saved = time(nullptr);
    payload.current_state = state_ == TimerState::RUNNING ? "running" : (state_ == TimerState::PAUSED ? "paused" : "stopped");
    payload.session_id = current_segment_.session_id;
    payload.segment_id = current_segment_.segment_id;
    payload.cycle_label = current_segment_.cycle_label;
    payload.segment_start_epoch = current_segment_.start_epoch;
    payload.elapsed_ms_in_segment = elapsed_ms_in_segment_;
    payload.remaining_ms = remaining_ms_;
    payload.volume_level = buzzer_.volumeLevel();
    payload.last_sync_epoch = last_sync_epoch_;
    payload.clock_offset_ms = 0;
    payload.phase_index = current_phase_index_;
    payload.work_count = work_cycle_count_;
    payload.break_count = break_cycle_count_;
    payload.today_focus_ms = today_focus_ms_;
    payload.current_date_key = current_date_key_;

    File file = LittleFS.open("/system/checkpoint.json", "w");
    if (!file)
    {
      return;
    }

    StaticJsonDocument<768> doc;
    doc["version"] = payload.version;
    doc["timestamp_saved"] = payload.timestamp_saved;
    doc["current_state"] = payload.current_state;
    doc["session_id"] = payload.session_id;
    doc["segment_id"] = payload.segment_id;
    doc["cycle_label"] = payload.cycle_label;
    doc["segment_start_epoch"] = payload.segment_start_epoch;
    doc["elapsed_ms_in_segment"] = payload.elapsed_ms_in_segment;
    doc["remaining_ms"] = payload.remaining_ms;
    doc["volume_level"] = payload.volume_level;
    doc["last_sync_epoch"] = payload.last_sync_epoch;
    doc["clock_offset_ms"] = payload.clock_offset_ms;
    doc["phase_index"] = payload.phase_index;
    doc["work_count"] = payload.work_count;
    doc["break_count"] = payload.break_count;
    doc["today_focus_ms"] = payload.today_focus_ms;
    doc["current_date_key"] = payload.current_date_key;

    serializeJson(doc, file);
    file.close();
  }

  // ----------------------------
  // 診断ヘルパー
  // ----------------------------
  void splash(const char *l1, const char *l2, uint16_t color)
  {
    tft_.fillScreen(COLOR_BACKGROUND);
    tft_.setTextWrap(false);
    tft_.setTextColor(color);
    tft_.setTextSize(2);
    tft_.setCursor(10, 20);
    tft_.print(l1);
    tft_.setCursor(10, 50);
    tft_.print(l2);
  }

  void drawBootBadges()
  {
    tft_.setTextSize(2);
    tft_.setTextColor(COLOR_TEXT_PRIMARY);
    tft_.fillRect(0, 80, 240, 110, COLOR_BACKGROUND);
    tft_.setCursor(10, 90);
    tft_.print("FS:");
    tft_.print(littlefs_available_ ? "OK" : "NG");
    tft_.setCursor(10, 112);
    tft_.print("SD:");
    tft_.print(sd_available_ ? "OK" : "NG");
    tft_.setCursor(10, 134);
    tft_.print("WiFi:");
    tft_.print(wifi_connected_ ? "OK" : "NG");
    tft_.setCursor(10, 156);
    tft_.print("Time:");
    tft_.print(time_synced_ ? "OK" : "NG");
  }

  void beepOk(uint8_t count)
  {
    for (uint8_t i = 0; i < count; ++i)
    {
      buzzer_.beep(80);
      delay(120);
    }
  }

  void beepError(uint8_t count, uint8_t min_volume = 3)
  {
    uint8_t prev = buzzer_.volumeLevel();
    uint8_t target = constrain(min_volume, static_cast<uint8_t>(1), static_cast<uint8_t>(10));
    if (prev < target)
      buzzer_.setVolumeLevel(target);
    for (uint8_t i = 0; i < count; ++i)
    {
      buzzer_.beep(200);
      delay(200);
    }
    buzzer_.setVolumeLevel(prev);
  }

  void appendCsvEntry(uint16_t session_id,
                      uint16_t segment_id,
                      time_t start_epoch,
                      time_t end_epoch,
                      uint32_t duration_ms,
                      SegmentEndReason reason,
                      uint8_t volume_level,
                      const char *cycle_label)
  {
    if (!sd_available_)
    {
      return;
    }

    File file = SD.open("/logs/work.csv", FILE_APPEND);
    if (!file)
    {
      return;
    }

    char start_buffer[24];
    char end_buffer[24];
    formatTimestamp(start_epoch, start_buffer, sizeof(start_buffer));
    formatTimestamp(end_epoch, end_buffer, sizeof(end_buffer));

    uint32_t duration_sec = duration_ms / 1000UL;

    file.printf("%u,%u,%s,%s,%lu,%s,%u,%s\n",
                session_id,
                segment_id,
                start_buffer,
                end_buffer,
                static_cast<unsigned long>(duration_sec),
                SEGMENT_REASON_STRINGS[static_cast<uint8_t>(reason)],
                static_cast<unsigned int>(volume_level),
                cycle_label);

    file.close();
  }

  void formatTimestamp(time_t epoch, char *out, size_t size)
  {
    if (epoch <= 0)
    {
      snprintf(out, size, "1970-01-01 00:00:00");
      return;
    }
    struct tm info;
    localtime_r(&epoch, &info);
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d",
             info.tm_year + 1900,
             info.tm_mon + 1,
             info.tm_mday,
             info.tm_hour,
             info.tm_min,
             info.tm_sec);
  }

  // ---------------------------------------------------------------------------
  // メンバー変数
  // ---------------------------------------------------------------------------
  ButtonHandler button_handler_;
  BuzzerController buzzer_;
  Adafruit_ST7789 tft_ = Adafruit_ST7789(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

  TimerState state_ = TimerState::STOPPED;
  uint8_t current_phase_index_ = 0;
  uint32_t remaining_ms_ = PHASES[0].duration_ms;
  uint32_t elapsed_ms_in_segment_ = 0;

  SegmentMetadata current_segment_;

  uint16_t work_cycle_count_ = 0;
  uint16_t break_cycle_count_ = 0;
  uint16_t next_session_id_ = 1;
  uint32_t today_focus_ms_ = 0;

  bool pre_alert_triggered_ = false;
  uint8_t final_countdown_last_sec_ = 0;

  uint32_t last_tick_ms_ = 0;
  uint32_t last_ui_render_ms_ = 0;
  uint32_t checkpoint_next_due_ms_ = 0;
  uint32_t last_diag_log_ms_ = 0;

  bool wifi_connected_ = false;
  bool time_synced_ = false;
  time_t last_sync_epoch_ = 0;
  uint32_t last_ntp_sync_ms_ = 0;
  bool sd_available_ = false;
  bool littlefs_available_ = false;

  char current_date_key_[9] = "19700101";
};

void ButtonHandler::handleLongPressTick(uint32_t now_ms)
{
  if (active_button_ != ButtonId::BTN1)
  {
    return;
  }
  if (long_press_observer_ == nullptr)
  {
    return;
  }
  if (last_long_press_tick_ms_ == 0)
  {
    return;
  }

  uint32_t held_ms = now_ms - press_start_ms_;
  if (held_ms < 1000)
  {
    return;
  }

  if ((now_ms - last_long_press_tick_ms_) < 1000)
  {
    return;
  }

  last_long_press_tick_ms_ = now_ms;
  uint32_t seconds = held_ms / 1000;
  long_press_observer_->onButtonLongPressTick(active_button_, seconds);
}

static void renderUI(PomodoroTimerApp &app)
{
  Adafruit_ST7789 &tft = app.display();

  struct UiCache
  {
    bool initialized;
    TimerState state;
    bool wifi_connected;
    bool sd_available;
    uint8_t phase_index;
    uint32_t remaining_seconds;
    uint32_t focus_minutes;
    uint8_t volume_level;
    char cycle_label[32];
    bool clock_valid;
    char clock_text[6];
  };

  static UiCache cache = {};

  bool full_redraw = !cache.initialized;
  if (!cache.initialized)
  {
    cache.cycle_label[0] = '\0';
    cache.clock_text[0] = '\0';
    cache.clock_valid = false;
  }

  if (!full_redraw && strcmp(app.cycleLabel(), cache.cycle_label) != 0)
  {
    full_redraw = true;
  }

  if (!full_redraw && app.currentPhaseIndex() != cache.phase_index)
  {
    full_redraw = true;
  }

  if (full_redraw)
  {
    tft.fillScreen(COLOR_BACKGROUND);
    tft.drawFastHLine(0, 72, 240, COLOR_RING_BG);
    tft.drawFastHLine(0, 188, 240, COLOR_RING_BG);
  }

  // ヘッダー: サイクルラベル
  if (full_redraw || strcmp(app.cycleLabel(), cache.cycle_label) != 0)
  {
    tft.fillRect(0, 0, 240, 30, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT_PRIMARY);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print(app.cycleLabel());
  }

  // ヘッダー: タイマーの状態
  if (full_redraw || app.state() != cache.state)
  {
    tft.fillRect(0, 30, 140, 30, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT_PRIMARY);
    tft.setTextSize(2);
    tft.setCursor(10, 40);
    switch (app.state())
    {
    case TimerState::RUNNING:
      tft.print("RUNNING");
      break;
    case TimerState::PAUSED:
      tft.print("PAUSED");
      break;
    case TimerState::STOPPED:
      tft.print("READY");
      break;
    }
  }

  // ヘッダー: 時計（右上）
  char clock_buffer[6] = "--:--";
  bool clock_available = false;
  struct tm clock_info;
  if (getLocalTime(&clock_info, 5))
  {
    snprintf(clock_buffer, sizeof(clock_buffer), "%02d:%02d", clock_info.tm_hour, clock_info.tm_min);
    clock_available = true;
  }

  bool clock_dirty = full_redraw || (clock_available != cache.clock_valid) ||
                     (clock_available && strcmp(clock_buffer, cache.clock_text) != 0);

  if (clock_dirty)
  {
    const int16_t area_x = 150;
    const int16_t area_y = 0;
    const int16_t area_width = 90;
    const int16_t area_height = 20;
    tft.fillRect(area_x, area_y, area_width, area_height, COLOR_BACKGROUND);
    tft.setTextSize(2);
    tft.setTextColor(clock_available ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DIM);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(clock_buffer, 0, 0, &x1, &y1, &w, &h);
    int16_t clock_x = area_x + (area_width - static_cast<int16_t>(w)) / 2;
    int16_t clock_y = area_y + (area_height - static_cast<int16_t>(h)) / 2 - y1;
    tft.setCursor(clock_x, clock_y);
    tft.print(clock_buffer);
  }

  // ヘッダー: SDカード利用可否
  if (full_redraw || app.sdAvailable() != cache.sd_available)
  {
    tft.fillRect(150, 20, 90, 12, COLOR_BACKGROUND);
    if (!app.sdAvailable())
    {
      tft.setTextColor(COLOR_ALERT);
      tft.setTextSize(1);
      tft.setCursor(150, 28);
      tft.print("SD!");
    }
  }

  // ヘッダー: Wi-Fiステータス
  if (full_redraw || app.wifiConnected() != cache.wifi_connected)
  {
    drawWifiIndicator(tft, 150, 30, app.wifiConnected());
  }

  // プログレスバーと残り時間
  uint32_t base = app.basePhaseDurationMs();
  uint32_t remaining = app.remainingMs();
  uint32_t total_seconds = (remaining + 999) / 1000;
  bool progress_dirty = full_redraw || (total_seconds != cache.remaining_seconds);

  if (progress_dirty)
  {
    float remaining_ratio = (base == 0) ? 0.0f : (static_cast<float>(remaining) / static_cast<float>(base));
    drawProgressBar(tft, remaining_ratio);
  }

  if (progress_dirty)
  {
    tft.fillRect(0, 132, 240, 48, COLOR_BACKGROUND);
    char time_buffer[16];
    if (total_seconds >= 3600)
    {
      uint32_t hours = total_seconds / 3600;
      uint32_t minutes = (total_seconds % 3600) / 60;
      uint32_t seconds = total_seconds % 60;
      snprintf(time_buffer, sizeof(time_buffer), "%u:%02u:%02u", hours, minutes, seconds);
    }
    else
    {
      uint32_t minutes = total_seconds / 60;
      uint32_t seconds = total_seconds % 60;
      snprintf(time_buffer, sizeof(time_buffer), "%02u:%02u", minutes, seconds);
    }
    drawCenteredText(tft, time_buffer, 144, 4, COLOR_TEXT_PRIMARY);
  }

  if (app.state() == TimerState::PAUSED)
  {
    if (full_redraw || cache.state != TimerState::PAUSED)
    {
      tft.fillRect(0, 180, 240, 24, COLOR_BACKGROUND);
      drawCenteredText(tft, "PAUSED", 188, 2, COLOR_ALERT);
    }
  }
  else if (cache.state == TimerState::PAUSED)
  {
    tft.fillRect(0, 180, 240, 24, COLOR_BACKGROUND);
  }

  uint32_t focus_minutes = app.todayFocusMs() / 60000UL;
  if (full_redraw || focus_minutes != cache.focus_minutes)
  {
    uint32_t focus_hours = focus_minutes / 60;
    uint32_t focus_rem_minutes = focus_minutes % 60;
    char focus_buffer[24];
    snprintf(focus_buffer, sizeof(focus_buffer), "Focus %02u:%02u", focus_hours, focus_rem_minutes);
    tft.fillRect(0, 200, 140, 24, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT_PRIMARY);
    tft.setTextSize(2);
    tft.setCursor(10, 206);
    tft.print(focus_buffer);
  }

  if (full_redraw || app.volumeLevel() != cache.volume_level)
  {
    char volume_buffer[16];
    snprintf(volume_buffer, sizeof(volume_buffer), "Vol %u", static_cast<unsigned>(app.volumeLevel()));
    tft.fillRect(140, 200, 100, 24, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT_PRIMARY);
    tft.setTextSize(2);
    tft.setCursor(150, 206);
    tft.print(volume_buffer);
  }

  cache.initialized = true;
  cache.state = app.state();
  cache.wifi_connected = app.wifiConnected();
  cache.sd_available = app.sdAvailable();
  cache.phase_index = app.currentPhaseIndex();
  cache.remaining_seconds = total_seconds;
  cache.focus_minutes = focus_minutes;
  cache.volume_level = app.volumeLevel();
  cache.clock_valid = clock_available;
  if (clock_available)
  {
    strncpy(cache.clock_text, clock_buffer, sizeof(cache.clock_text) - 1);
    cache.clock_text[sizeof(cache.clock_text) - 1] = '\0';
  }
  else
  {
    cache.clock_text[0] = '\0';
  }
  strncpy(cache.cycle_label, app.cycleLabel(), sizeof(cache.cycle_label) - 1);
  cache.cycle_label[sizeof(cache.cycle_label) - 1] = '\0';
}

static void drawWifiIndicator(Adafruit_ST7789 &display, int16_t x, int16_t y, bool connected)
{
  const int16_t width = 90;
  const int16_t height = 20;
  display.fillRect(x, y, width, height, COLOR_BACKGROUND);

  uint16_t icon_color = connected ? COLOR_BREAK : COLOR_TEXT_DIM;
  int16_t base_x = x + 6;
  int16_t base_y = y + height - 5;

  // 3本のシグナルバーを描画; 接続時は塗りつぶし、オフライン時はアウトラインで一目で分かるように
  for (int i = 0; i < 3; ++i)
  {
    int16_t bar_height = (i + 1) * 4;
    int16_t bar_x = base_x + i * 6;
    int16_t bar_y = base_y - bar_height;
    if (connected)
    {
      display.fillRect(bar_x, bar_y, 4, bar_height, icon_color);
    }
    else
    {
      display.drawRect(bar_x, bar_y, 4, bar_height, icon_color);
    }
  }

  if (connected)
  {
    display.fillCircle(base_x + 2, base_y + 1, 2, icon_color);
  }
  else
  {
    display.drawCircle(base_x + 2, base_y + 1, 2, icon_color);
    display.drawLine(x + width - 18, y + 4, x + width - 4, y + height - 4, COLOR_ALERT);
    display.drawLine(x + width - 18, y + height - 4, x + width - 4, y + 4, COLOR_ALERT);
  }

  display.setTextSize(2);
  display.setCursor(x + 28, y + 4);
  if (connected)
  {
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.print("WiFi");
  }
  else
  {
    display.setTextColor(COLOR_ALERT);
    display.print("OFF");
  }
}

static void drawProgressBar(Adafruit_ST7789 &display, float remaining_ratio)
{
  const int16_t bar_x = 20;
  const int16_t bar_y = 90;
  const int16_t bar_width = 200;
  const int16_t bar_height = 40;
  const int16_t inner_x = bar_x + 2;
  const int16_t inner_y = bar_y + 2;
  const int16_t inner_width = bar_width - 4;
  const int16_t inner_height = bar_height - 4;

  display.fillRoundRect(bar_x, bar_y, bar_width, bar_height, 8, COLOR_BAR_BG);
  display.fillRoundRect(inner_x, inner_y, inner_width, inner_height, 6, COLOR_BACKGROUND);

  float clamped = remaining_ratio;
  if (clamped < 0.0f)
  {
    clamped = 0.0f;
  }
  else if (clamped > 1.0f)
  {
    clamped = 1.0f;
  }

  int16_t fill_width = static_cast<int16_t>(inner_width * clamped + 0.5f);
  if (fill_width > 0)
  {
    display.fillRect(inner_x, inner_y, fill_width, inner_height, COLOR_TEXT_PRIMARY);
  }

  uint16_t percent = static_cast<uint16_t>(clamped * 100.0f + 0.5f);
  char percent_buffer[8];
  snprintf(percent_buffer, sizeof(percent_buffer), "%u%%", percent);
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(2);
  display.setTextColor(COLOR_TEXT_PRIMARY);
  display.getTextBounds(percent_buffer, 0, 0, &x1, &y1, &w, &h);
  int16_t text_x = bar_x + (bar_width - static_cast<int16_t>(w)) / 2;
  int16_t text_y = bar_y + (bar_height - static_cast<int16_t>(h)) / 2 - y1;
  display.setCursor(text_x, text_y);
  display.print(percent_buffer);
}

static void drawCenteredText(Adafruit_ST7789 &display, const char *text, int16_t y, uint8_t size, uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.setTextColor(color);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

// -----------------------------------------------------------------------------
// グローバルアプリケーションインスタンス
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

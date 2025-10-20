/*
 * ポモドーロタイマーアプリケーション - ポモドーロタイマー v0
 * メインアプリケーションロジック
 */

#ifndef POMODORO_TIMER_APP_H
#define POMODORO_TIMER_APP_H

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>

#include "config.h"
#include "timer_types.h"
#include "button_handler.h"
#include "buzzer_controller.h"

// -----------------------------------------------------------------------------
// ポモドーロタイマーアプリケーションクラス
// -----------------------------------------------------------------------------
class PomodoroTimerApp : public IButtonLongPressObserver
{
public:
  void begin();
  void loop();

  // UI / ステータス用のアクセサ
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
  uint32_t elapsedMsInSegment() const { return elapsed_ms_in_segment_; }
  time_t lastSyncEpoch() const { return last_sync_epoch_; }

  // IButtonLongPressObserver実装
  void onButtonLongPressTick(ButtonId button, uint32_t seconds_elapsed) override;

private:
  // 初期化ヘルパー
  void initDisplay();
  void initStorage();
  void initWiFiAndTime();
  bool waitForTimeSync(struct tm &info);
  void loadCheckpointOrReset();
  void restoreFromCheckpoint(const CheckpointPayload &payload);
  void logRecoveredDrop(const CheckpointPayload &payload);
  void resetState();

  // ボタン処理
  void processButtonEvents();
  void handleStartPause();
  void handleSkip();
  void handlePrev();
  void handleFactoryReset();
  void adjustRemainingMinutes(int8_t delta_minutes);
  void adjustVolume(int8_t delta);

  // フェーズ / セグメント制御
  void beginPhase(uint8_t phase_index);
  void resumeCurrentPhase();
  void advancePhase(int direction, bool auto_start);
  void concludeCycleAndHold();
  void updateTimer(uint32_t delta_ms);
  void triggerAlerts();
  void finalizeSegment(SegmentEndReason reason);
  time_t determineSegmentStartEpoch();
  time_t determineSegmentEndEpoch();
  void incrementSessionId();
  void ensureDateKeyFresh();
  void updateCurrentDateKey();
  void buildCurrentDateKey(char *out, size_t size);
  void maybeHandleDayCutover();
  void maybeSaveCheckpoint(uint32_t now_ms);

  // 診断ヘルパー
  void splash(const char *l1, const char *l2, uint16_t color);
  void drawBootBadges();
  void beepOk(uint8_t count);
  void beepError(uint8_t count, uint8_t min_volume = 3);
  void appendCsvEntry(uint16_t session_id,
                      uint16_t segment_id,
                      time_t start_epoch,
                      time_t end_epoch,
                      uint32_t duration_ms,
                      SegmentEndReason reason,
                      uint8_t volume_level,
                      const char *cycle_label);
  void formatTimestamp(time_t epoch, char *out, size_t size);

  // メンバー変数
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

#endif // POMODORO_TIMER_APP_H

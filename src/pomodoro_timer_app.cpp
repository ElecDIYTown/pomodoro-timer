/*
 * ポモドーロタイマーアプリケーション実装 - ポモドーロタイマー v0
 */

#include "pomodoro_timer_app.h"
#include "ui_renderer.h"

void PomodoroTimerApp::begin()
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

void PomodoroTimerApp::loop()
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

void PomodoroTimerApp::onButtonLongPressTick(ButtonId button, uint32_t seconds_elapsed)
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

// ---------------------------------------------------------------------------
// 初期化ヘルパー
// ---------------------------------------------------------------------------
void PomodoroTimerApp::initDisplay()
{
  DBG_PRINTLN("[BOOT] initDisplay: ST7789 init start");
  tft_ = Adafruit_ST7789(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
  tft_.init(240, 240);
  tft_.setRotation(2); // USBコネクタが下
  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextWrap(false);
  // 視覚的チェック用のシンプルなボーダー
  tft_.fillRect(0, 0, 240, 6, ST77XX_BLUE);
  tft_.fillRect(0, 234, 240, 6, ST77XX_GREEN);
  DBG_PRINTLN("[BOOT] initDisplay: OK");
}

void PomodoroTimerApp::initStorage()
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

void PomodoroTimerApp::initWiFiAndTime()
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

  // Check if WiFi sync was already done today
  char today_key[9] = {0};
  buildCurrentDateKey(today_key, sizeof(today_key));
  
  if (strcmp(today_key, last_sync_date_key_) == 0 && strcmp(today_key, "19700101") != 0)
  {
    DBG_PRINTLN("[WiFi] Skipping sync - already synced today");
    wifi_connected_ = false;
    time_synced_ = (last_sync_epoch_ > 0);
    
    // Configure time even if not syncing
    configTime(TIMEZONE_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    setenv("TZ", "JST-9", 1);
    tzset();
    return;
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
      strncpy(last_sync_date_key_, today_key, sizeof(last_sync_date_key_));
      last_sync_date_key_[sizeof(last_sync_date_key_) - 1] = '\0';
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

bool PomodoroTimerApp::waitForTimeSync(struct tm &info)
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

void PomodoroTimerApp::reconnectWiFiAndSyncTime()
{
  DBG_PRINTLN("[WiFi] Manual reconnect and sync requested");
  
  // Disconnect WiFi if connected
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
    delay(100);
  }
  
  // Connect to WiFi
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

  DBG_PRINTLN(wifi_connected_ ? "[WiFi] Reconnected" : "[WiFi] Reconnect failed");

  if (wifi_connected_)
  {
    // Reconfigure time
    configTime(TIMEZONE_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    setenv("TZ", "JST-9", 1);
    tzset();
    
    struct tm timeinfo;
    if (waitForTimeSync(timeinfo))
    {
      time_synced_ = true;
      last_sync_epoch_ = time(nullptr);
      last_ntp_sync_ms_ = millis();
      
      // Update last sync date key
      char today_key[9] = {0};
      buildCurrentDateKey(today_key, sizeof(today_key));
      strncpy(last_sync_date_key_, today_key, sizeof(last_sync_date_key_));
      last_sync_date_key_[sizeof(last_sync_date_key_) - 1] = '\0';
      
      DBG_PRINTLN("[TIME] Manual NTP sync OK");
      uint8_t previous_volume = buzzer_.volumeLevel();
      buzzer_.setVolumeLevel(1);
      beepOk(3);
      buzzer_.setVolumeLevel(previous_volume);
    }
    else
    {
      DBG_PRINTLN("[TIME] Manual NTP sync FAIL");
      beepError(2, 1);
    }
  }
  else
  {
    time_synced_ = false;
    beepError(3, 1);
  }
}

void PomodoroTimerApp::loadCheckpointOrReset()
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
  payload.last_sync_date_key = (const char *)(doc["last_sync_date_key"] | "");

  restoreFromCheckpoint(payload);
}

void PomodoroTimerApp::restoreFromCheckpoint(const CheckpointPayload &payload)
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
  strncpy(last_sync_date_key_, payload.last_sync_date_key.c_str(), sizeof(last_sync_date_key_));
  last_sync_date_key_[sizeof(last_sync_date_key_) - 1] = '\0';
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
    // 再開のためにセグメントIDをインクリメント
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

void PomodoroTimerApp::logRecoveredDrop(const CheckpointPayload &payload)
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

void PomodoroTimerApp::resetState()
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
void PomodoroTimerApp::processButtonEvents()
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
    case ButtonEventType::BTN1_LONG_15S:
      handleWiFiSync();
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

void PomodoroTimerApp::handleStartPause()
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

void PomodoroTimerApp::handleSkip()
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

void PomodoroTimerApp::handlePrev()
{
  if (state_ == TimerState::STOPPED)
  {
    advancePhase(-1, false);
    return;
  }

  finalizeSegment(SegmentEndReason::PREV);
  advancePhase(-1, true);
}

void PomodoroTimerApp::handleFactoryReset()
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

void PomodoroTimerApp::handleWiFiSync()
{
  DBG_PRINTLN("[BTN] WiFi sync requested");
  reconnectWiFiAndSyncTime();
}

void PomodoroTimerApp::adjustRemainingMinutes(int8_t delta_minutes)
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

void PomodoroTimerApp::adjustVolume(int8_t delta)
{
  int16_t level = static_cast<int16_t>(buzzer_.volumeLevel()) + delta;
  level = constrain(level, static_cast<int16_t>(0), static_cast<int16_t>(10));
  buzzer_.setVolumeLevel(level);
}

// ---------------------------------------------------------------------------
// フェーズ / セグメント制御
// ---------------------------------------------------------------------------
void PomodoroTimerApp::beginPhase(uint8_t phase_index)
{
  current_phase_index_ = phase_index % PHASE_COUNT;
  bool is_work = PHASES[current_phase_index_].is_work;

  if (is_work)
  {
    work_cycle_count_ += 1;
    snprintf(current_segment_.cycle_label, sizeof(current_segment_.cycle_label), "Work#%u", work_cycle_count_);
    // 新しい作業セッション => セッションIDをインクリメントし、セグメントIDをリセット
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

void PomodoroTimerApp::resumeCurrentPhase()
{
  state_ = TimerState::RUNNING;
  current_segment_.segment_id += 1;
  current_segment_.start_epoch = determineSegmentStartEpoch();
  elapsed_ms_in_segment_ = 0;
}

void PomodoroTimerApp::advancePhase(int direction, bool auto_start)
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

// 設定されたサイクルが終了したらタイマーの状態をリセットする
void PomodoroTimerApp::concludeCycleAndHold()
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

void PomodoroTimerApp::updateTimer(uint32_t delta_ms)
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

void PomodoroTimerApp::triggerAlerts()
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

void PomodoroTimerApp::finalizeSegment(SegmentEndReason reason)
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
    // 実行中の状態を停止 (呼び出し元によって設定される)
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

time_t PomodoroTimerApp::determineSegmentStartEpoch()
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

time_t PomodoroTimerApp::determineSegmentEndEpoch()
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

void PomodoroTimerApp::incrementSessionId()
{
  // 日付が変わったときにセッションカウンターをリセット
  ensureDateKeyFresh();
  current_segment_.session_id = next_session_id_;
  next_session_id_ += 1;
}

void PomodoroTimerApp::ensureDateKeyFresh()
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

void PomodoroTimerApp::updateCurrentDateKey()
{
  buildCurrentDateKey(current_date_key_, sizeof(current_date_key_));
  current_date_key_[sizeof(current_date_key_) - 1] = '\0';
}

void PomodoroTimerApp::buildCurrentDateKey(char *out, size_t size)
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

void PomodoroTimerApp::maybeHandleDayCutover()
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

  // 日付変更 -> 現在のセグメントを午前0時に終了
  time_t now_epoch = time(nullptr);
  struct tm info;
  if (!getLocalTime(&info, 50))
  {
    ensureDateKeyFresh();
    return;
  }

  // 新しい日の午前0時のエポックを計算
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
  // remaining_ms_ は既に残りを反映している (一部が消費されているため)
  elapsed_ms_in_segment_ = 0;
  pre_alert_triggered_ = (remaining_ms_ <= PRE_ALERT_THRESHOLD_MS);
  final_countdown_last_sec_ = 0;
}

void PomodoroTimerApp::maybeSaveCheckpoint(uint32_t now_ms)
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
  payload.last_sync_date_key = last_sync_date_key_;

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
  doc["last_sync_date_key"] = payload.last_sync_date_key;

  serializeJson(doc, file);
  file.close();
}

// ----------------------------
// 診断ヘルパー
// ----------------------------
void PomodoroTimerApp::splash(const char *l1, const char *l2, uint16_t color)
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

void PomodoroTimerApp::drawBootBadges()
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

void PomodoroTimerApp::beepOk(uint8_t count)
{
  for (uint8_t i = 0; i < count; ++i)
  {
    buzzer_.beep(80);
    delay(120);
  }
}

void PomodoroTimerApp::beepError(uint8_t count, uint8_t min_volume)
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

void PomodoroTimerApp::appendCsvEntry(uint16_t session_id,
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

void PomodoroTimerApp::formatTimestamp(time_t epoch, char *out, size_t size)
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

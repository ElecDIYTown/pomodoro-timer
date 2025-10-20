/*
 * UIレンダラー実装 - ポモドーロタイマー v0
 */

#include "ui_renderer.h"
#include "pomodoro_timer_app.h"
#include <time.h>

void renderUI(PomodoroTimerApp &app)
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

  // ヘッダー: 時計 (右上)
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

  // ヘッダー: SD利用可否
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

void drawWifiIndicator(Adafruit_ST7789 &display, int16_t x, int16_t y, bool connected)
{
  const int16_t width = 90;
  const int16_t height = 20;
  display.fillRect(x, y, width, height, COLOR_BACKGROUND);

  uint16_t icon_color = connected ? COLOR_BREAK : COLOR_TEXT_DIM;
  int16_t base_x = x + 6;
  int16_t base_y = y + height - 5;

  // 3本のシグナルバーを描画; 接続時は塗りつぶし、オフライン時はアウトラインで一目で分かるようにする
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

void drawProgressBar(Adafruit_ST7789 &display, float remaining_ratio)
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

void drawCenteredText(Adafruit_ST7789 &display, const char *text, int16_t y, uint8_t size, uint16_t color)
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

/*
 * UIレンダラー - ポモドーロタイマー v0
 * TFTディスプレイへのUI描画
 */

#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"
#include "timer_types.h"

// 前方宣言
class PomodoroTimerApp;

// -----------------------------------------------------------------------------
// UI描画関数
// -----------------------------------------------------------------------------
void renderUI(PomodoroTimerApp &app);
void drawProgressBar(Adafruit_ST7789 &display, float remaining_ratio);
void drawWifiIndicator(Adafruit_ST7789 &display, int16_t x, int16_t y, bool connected);
void drawCenteredText(Adafruit_ST7789 &display, const char *text, int16_t y, uint8_t size, uint16_t color);

#endif // UI_RENDERER_H

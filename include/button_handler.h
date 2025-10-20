/*
 * ボタンハンドラ - ポモドーロタイマー v0
 * ラダー抵抗方式の3ボタン入力、デバウンス、ダブルタップ、長押し検出
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
// ボタンID
// -----------------------------------------------------------------------------
enum class ButtonId : uint8_t
{
  NONE = 0,
  BTN1,
  BTN2,
  BTN3
};

// -----------------------------------------------------------------------------
// ボタンイベントタイプ
// -----------------------------------------------------------------------------
enum class ButtonEventType : uint8_t
{
  NONE = 0,
  BTN1_SHORT,
  BTN1_LONG_2S,
  BTN1_LONG_4S,
  BTN1_LONG_10S,
  BTN1_LONG_15S,
  BTN2_SINGLE,
  BTN2_DOUBLE,
  BTN3_SINGLE,
  BTN3_DOUBLE
};

// -----------------------------------------------------------------------------
// ボタンイベント
// -----------------------------------------------------------------------------
struct ButtonEvent
{
  ButtonEventType type = ButtonEventType::NONE;
};

// -----------------------------------------------------------------------------
// 保留中シングルタップ
// -----------------------------------------------------------------------------
struct PendingSingle
{
  bool active = false;
  uint32_t timestamp_ms = 0;
};

// -----------------------------------------------------------------------------
// 長押しオブザーバーインターフェース
// -----------------------------------------------------------------------------
class IButtonLongPressObserver
{
public:
  virtual ~IButtonLongPressObserver() = default;
  virtual void onButtonLongPressTick(ButtonId button, uint32_t seconds_elapsed) = 0;
};

// -----------------------------------------------------------------------------
// ボタンハンドラクラス
// -----------------------------------------------------------------------------
class ButtonHandler
{
public:
  void begin();
  void setLongPressObserver(IButtonLongPressObserver *observer);
  void update(uint32_t now_ms);
  bool poll(ButtonEvent &evt);

private:
  static constexpr uint8_t QUEUE_CAPACITY = 8;

  ButtonId decodeButton(uint16_t raw);
  void updateDebounce(ButtonId raw, uint32_t now_ms);
  void handleStateChange(ButtonId previous, ButtonId current, uint32_t now_ms);
  void handleRelease(ButtonId button, uint32_t now_ms);
  void handleTap(ButtonId button, uint32_t held_ms, uint32_t now_ms);
  void finalizePendingSingles(uint32_t now_ms);
  void finalizeSingleForButton(ButtonId button, PendingSingle &slot, uint32_t now_ms);
  void enqueue(ButtonEventType type);
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

  IButtonLongPressObserver *long_press_observer_ = nullptr;
};

#endif // BUTTON_HANDLER_H

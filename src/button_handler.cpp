/*
 * ボタンハンドラ実装 - ポモドーロタイマー v0
 */

#include "button_handler.h"

void ButtonHandler::begin()
{
  analogReadResolution(12);
  pinMode(PIN_BUTTON_LADDER, INPUT);
}

void ButtonHandler::setLongPressObserver(IButtonLongPressObserver *observer)
{
  long_press_observer_ = observer;
}

void ButtonHandler::update(uint32_t now_ms)
{
  ButtonId raw = decodeButton(analogRead(PIN_BUTTON_LADDER));
  updateDebounce(raw, now_ms);
  finalizePendingSingles(now_ms);
  handleLongPressTick(now_ms);
}

bool ButtonHandler::poll(ButtonEvent &evt)
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

ButtonId ButtonHandler::decodeButton(uint16_t raw)
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

void ButtonHandler::updateDebounce(ButtonId raw, uint32_t now_ms)
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

void ButtonHandler::handleStateChange(ButtonId previous, ButtonId current, uint32_t now_ms)
{
  if (previous == ButtonId::NONE && current != ButtonId::NONE)
  {
    // ボタン押下
    active_button_ = current;
    press_start_ms_ = now_ms;
    last_long_press_tick_ms_ = now_ms;
  }
  else if (previous != ButtonId::NONE && current == ButtonId::NONE)
  {
    // ボタン解放
    handleRelease(previous, now_ms);
    active_button_ = ButtonId::NONE;
    last_long_press_tick_ms_ = 0;
  }
}

void ButtonHandler::handleRelease(ButtonId button, uint32_t now_ms)
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

void ButtonHandler::handleTap(ButtonId button, uint32_t /*held_ms*/, uint32_t now_ms)
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

void ButtonHandler::finalizePendingSingles(uint32_t now_ms)
{
  finalizeSingleForButton(ButtonId::BTN2, pending_btn2_, now_ms);
  finalizeSingleForButton(ButtonId::BTN3, pending_btn3_, now_ms);
}

void ButtonHandler::finalizeSingleForButton(ButtonId button, PendingSingle &slot, uint32_t now_ms)
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

void ButtonHandler::enqueue(ButtonEventType type)
{
  if (type == ButtonEventType::NONE)
  {
    return;
  }
  if (queue_count_ >= QUEUE_CAPACITY)
  {
    // 最新の操作を応答性良く保つため、最も古いものをドロップ
    queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
    queue_count_--;
  }
  uint8_t index = (queue_head_ + queue_count_) % QUEUE_CAPACITY;
  event_queue_[index].type = type;
  queue_count_++;
}

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

/*
 * タイマー型定義 - ポモドーロタイマー v0
 * タイマー状態、セグメント終了理由、メタデータ、チェックポイントペイロード
 */

#ifndef TIMER_TYPES_H
#define TIMER_TYPES_H

#include <Arduino.h>
#include <time.h>

// -----------------------------------------------------------------------------
// タイマー状態
// -----------------------------------------------------------------------------
enum class TimerState : uint8_t
{
  STOPPED = 0,
  RUNNING,
  PAUSED
};

// -----------------------------------------------------------------------------
// セグメント終了理由
// -----------------------------------------------------------------------------
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
    "completed",     // COMPLETED
    "paused",        // PAUSED
    "reset",         // RESET
    "skipped",       // SKIPPED
    "prev",          // PREV
    "day_cutover",   // DAY_CUTOVER
    "recovered_drop" // RECOVERED_DROP
};

// -----------------------------------------------------------------------------
// セグメントメタデータ
// -----------------------------------------------------------------------------
struct SegmentMetadata
{
  uint16_t session_id = 1;
  uint16_t segment_id = 1;
  bool is_work = true;
  char cycle_label[16] = "Work#1";
  time_t start_epoch = 0;
};

// -----------------------------------------------------------------------------
// チェックポイントペイロード
// -----------------------------------------------------------------------------
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
  String last_sync_date_key;
};

#endif // TIMER_TYPES_H

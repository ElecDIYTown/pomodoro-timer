# ポモドーロタイマー v0 実装メモ（確定事項）

## ピン割り当て

- 仕様書記載の GPIO 割り当てをそのまま採用する。
- SPI 共通バス: SCK=D8(GPIO8)、MOSI=D10(GPIO10)、MISO=D9(GPIO9)
- TFT: CS=D6(GPIO21)、DC=D2(GPIO4)、RST=ソフトリセット
- microSD: CS=D7(GPIO20)
- ボタン（アナログラダー）: D0(GPIO2)
- ブザー（PWM）: D1(GPIO3)
- 予備 I²C: D4/D5(GPIO6/7)

## Wi-Fi / NTP / タイムゾーン

- Wi-Fi SSID/パスワードはコード内にハードコードする（将来拡張余地を残す）。
- タイムゾーンは JST (Asia/Tokyo) をデフォルトとし、NTP 同期（起動時＋ 1 時間ごと）。

## ボタン入力方針

- ラダー構成: プルアップ 100kΩ（3.3V 側）、各ボタンは 10kΩ / 47kΩ / 68kΩ を介して GND へ接続。
- ADC 閾値は上記構成を基準にしつつ、キャリブレーション定数で調整可能にする。
- デバウンス 20ms、ダブルタップ判定 300ms、長押し 2s/4s。

## タイマー制御

- サイクル: Work50 → Break5 → Work50 → Break10（固定）。
- Next（長押し 2 秒）: 現在のサイクルを `skipped` として終了し、次に遷移。
- Prev（長押し 4 秒）: 現在のサイクルを `prev` として終了し、直前サイクルを再実行。

## CSV ログ

- `/logs/work.csv` に追記。ローテーションなし（月 1 で PC に退避）。
- `session_id` はローカル日付切替でリセット。`day_cutover` 行で日跨ぎを明示。
- `recovered_drop` は電源断復帰時に checkpoint から補完。
- USB ログ出力は基本無効（例外的なデバッグ時のみ任意実装）。

## checkpoint.json

- 保存場所: LittleFS `/system/checkpoint.json`。60 秒ごとに更新。
- 保存フィールド（最小セット）:
  - `version`
  - `timestamp_saved`
  - `current_state`（"work"/"break"/"paused" 等）
  - `session_id`
  - `segment_id`
  - `cycle_label`
  - `segment_start_epoch`
  - `elapsed_ms_in_segment`
  - `remaining_ms`
  - `volume_level`
  - `last_sync_epoch`
  - `clock_offset_ms`
- 復旧時は `recovered_drop` エントリを生成し、同 session で再開する際は `segment_id` をインクリメント。

## ブザー制御

- 受動ブザーに 2kHz トーンを出力。
- 音量 0〜10 は PWM デューティ比 0〜100% に線形マップ。
- 1 分前: 300ms ビープ 1 回。終了 5〜1 秒: 各 150ms ビープ。

## TFT 表示

- リング UI は機能優先でシンプルに実装。
- Work 時は青色ベース、Break は別色（例: 緑）。
- 60 分未満は `mm:ss`、60 分以上は `h:mm:ss` 表記。

## その他

- 月 1 回程度で PC へ CSV を手動バックアップ。
- USB ログなし前提のため、画面／SD 書込みで状況把握できる実装にする。

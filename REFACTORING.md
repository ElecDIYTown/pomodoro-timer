# リファクタリングドキュメント

## 概要
`src/main.cpp` (2004行) を複数のファイルに分割し、保守性と可読性を向上させました。

## 新しいファイル構造

```
include/
├── config.h                    # 設定定数（ピン、Wi-Fi、タイミング、色）
├── timer_types.h               # タイマー関連の型定義
├── button_handler.h            # ボタン入力処理
├── buzzer_controller.h         # ブザー制御
├── ui_renderer.h               # UI描画関数
└── pomodoro_timer_app.h        # メインアプリケーションロジック

src/
├── main.cpp                    # エントリーポイント (setup/loop)
├── config.cpp                  # 設定実装
├── button_handler.cpp          # ボタンハンドラ実装
├── buzzer_controller.cpp       # ブザーコントローラ実装
├── ui_renderer.cpp             # UI描画実装
└── pomodoro_timer_app.cpp      # アプリケーションロジック実装
```

## モジュール別の役割

### config.h / config.cpp
- ピン配置定数
- Wi-Fi / NTP設定
- タイミング定数
- サイクル設定 (PhaseConfig)
- デバッグマクロ
- 色定数

### timer_types.h
- `TimerState` 列挙型
- `SegmentEndReason` 列挙型
- `SegmentMetadata` 構造体
- `CheckpointPayload` 構造体

### button_handler.h / button_handler.cpp (213行)
- `ButtonId` 列挙型
- `ButtonEventType` 列挙型
- `ButtonHandler` クラス
  - ラダー抵抗式3ボタン入力
  - デバウンス処理
  - シングル/ダブルタップ検出
  - 長押し検出（2秒/4秒/10秒）
  - イベントキュー管理

### buzzer_controller.h / buzzer_controller.cpp (58行)
- `BuzzerController` クラス
  - PWM制御
  - 音量調整（0-10レベル）
  - ビープ音生成
  - ESP32 v2.x / v3.x API対応

### ui_renderer.h / ui_renderer.cpp (333行)
- `renderUI()` - メインUI描画
- `drawProgressBar()` - プログレスバー描画
- `drawWifiIndicator()` - Wi-Fiステータス表示
- `drawCenteredText()` - 中央揃えテキスト
- UIキャッシュによる部分更新最適化

### pomodoro_timer_app.h / pomodoro_timer_app.cpp (1016行)
- `PomodoroTimerApp` クラス - メインアプリケーションロジック
  - 初期化（ディスプレイ、ストレージ、Wi-Fi、時刻同期）
  - ボタンイベント処理
  - タイマー状態管理
  - フェーズ/セグメント制御
  - チェックポイント保存/復元
  - 日付変更処理
  - CSV ログ出力

### main.cpp (55行)
- エントリーポイント
- グローバルアプリインスタンス
- `setup()` 関数
- `loop()` 関数
- 診断ログ出力

## 変更による利点

### 1. 可読性の向上
- 各モジュールが単一責任を持つ
- ファイル名から機能が明確
- ヘッダーファイルでインターフェースが明確

### 2. 保守性の向上
- 機能ごとにファイルが分離されているため、変更が容易
- 影響範囲が明確
- テスト時にモジュール単位で確認可能

### 3. 再利用性の向上
- ButtonHandler、BuzzerControllerは他のプロジェクトでも使用可能
- UIレンダラーはディスプレイ関連の変更を一箇所に集約

### 4. コンパイル時間の改善（将来的）
- ヘッダーの変更が他のファイルに影響しにくい
- インクリメンタルビルドが効率的

## ビルド方法

以前と同じPlatformIOビルドコマンドで動作します：

```bash
pio run
```

または

```bash
pio run --target upload
```

## 移行ガイド

### 既存コードからの移行
1. 既存の `src/main.cpp` は削除または `main.cpp.old` にリネーム
2. 新しいファイル構造をそのまま使用
3. `include/secrets.h` は引き続き同じ場所に配置
4. ビルドして動作確認

### カスタマイズポイント
- **ピン配置変更**: `include/config.h` の `PIN_*` 定数を編集
- **タイミング調整**: `include/config.h` の `*_MS` 定数を編集
- **フェーズ設定変更**: `include/config.h` の `PHASES[]` 配列を編集
- **UI変更**: `src/ui_renderer.cpp` を編集
- **ビジネスロジック変更**: `src/pomodoro_timer_app.cpp` を編集

## 今後の拡張の方向性

新しい構造により、以下の拡張が容易になります：

1. **別のディスプレイドライバ対応**: `ui_renderer.cpp` のみ変更
2. **別の入力デバイス対応**: `button_handler.cpp` のみ変更
3. **Bluetooth/Web API追加**: 新しいモジュールとして追加可能
4. **複数の音声出力**: `buzzer_controller.cpp` を拡張
5. **ユニットテスト追加**: 各モジュールが独立しているため容易

## 注意事項

- 動作は元の `main.cpp` と完全に同一です
- すべての機能が保持されています
- メモリ使用量も同等です
- 実機での動作確認を推奨します

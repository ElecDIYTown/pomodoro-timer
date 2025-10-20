# アーキテクチャ図

## モジュール依存関係

```
main.cpp
  │
  ├─→ config.h (設定定数)
  │
  └─→ pomodoro_timer_app.h
        │
        ├─→ config.h
        ├─→ timer_types.h (データ構造)
        ├─→ button_handler.h
        │     └─→ config.h
        │
        ├─→ buzzer_controller.h
        │     └─→ config.h
        │
        └─→ ui_renderer.h
              └─→ config.h
              └─→ timer_types.h
```

## モジュール説明

### レイヤー1: 設定と型定義
- **config.h/cpp**: グローバル設定、定数、マクロ
- **timer_types.h**: 列挙型とデータ構造

### レイヤー2: ハードウェア抽象化
- **button_handler.h/cpp**: ボタン入力の抽象化
- **buzzer_controller.h/cpp**: ブザー出力の抽象化
- **ui_renderer.h/cpp**: ディスプレイ描画の抽象化

### レイヤー3: アプリケーションロジック
- **pomodoro_timer_app.h/cpp**: メインビジネスロジック
  - タイマー状態管理
  - フェーズ制御
  - データ永続化
  - イベント処理

### レイヤー4: エントリーポイント
- **main.cpp**: Arduino setup()とloop()

## データフロー

```
ボタン入力
    ↓
ButtonHandler
    ↓
ButtonEvent
    ↓
PomodoroTimerApp
    ├─→ BuzzerController (音声フィードバック)
    ├─→ TimerState更新
    └─→ UIRenderer (画面更新)
```

## ファイルサイズ比較

| ファイル | 行数 | 役割 |
|---------|-----|------|
| **元のmain.cpp** | 2004 | すべて |
| **新main.cpp** | 56 | エントリーポイント |
| pomodoro_timer_app.cpp | 1017 | メインロジック |
| ui_renderer.cpp | 334 | UI描画 |
| button_handler.cpp | 214 | ボタン処理 |
| buzzer_controller.cpp | 59 | ブザー制御 |
| config.cpp | 29 | 設定実装 |
| **合計 (実装)** | 1709 | |
| **ヘッダー** | 531 | |
| **総計** | 2240 | |

## 保守性の向上ポイント

1. **機能追加が容易**
   - 新しい入力デバイス → button_handler.cppのみ
   - 新しい出力デバイス → buzzer_controller.cppのみ
   - UIデザイン変更 → ui_renderer.cppのみ

2. **バグ修正が局所的**
   - ボタンの問題 → button_handler.cpp
   - 表示の問題 → ui_renderer.cpp
   - タイマーロジック → pomodoro_timer_app.cpp

3. **テストが容易**
   - 各モジュールを個別にテスト可能
   - モックオブジェクトの作成が容易

4. **チーム開発に適している**
   - 異なるメンバーが異なるモジュールを担当可能
   - マージコンフリクトが減少

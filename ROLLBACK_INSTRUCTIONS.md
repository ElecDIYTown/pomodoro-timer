# ロールバック手順

万が一、リファクタリング後に問題が発生した場合のロールバック手順です。

## オプション1: コミット単位でロールバック

### 元のmain.cppに完全に戻す

```bash
# リファクタリング前のコミットに戻す
git checkout f8513cf

# または、リファクタリングコミットを取り消す
git revert 4fc47ba
```

### 最新の変更のみ取り消す

```bash
# 最新のコミットを取り消す
git revert HEAD

# または、特定のコミットを取り消す
git revert <commit-hash>
```

## オプション2: ブランチを切り替える

```bash
# 元のブランチに戻る
git checkout main

# リファクタリングブランチを削除する（ローカル）
git branch -D copilot/refactor-main-cpp-file

# リファクタリングブランチを削除する（リモート）
git push origin --delete copilot/refactor-main-cpp-file
```

## オプション3: 特定のファイルのみ復元

```bash
# 元のmain.cppのみを復元
git checkout f8513cf -- src/main.cpp

# 新しいファイルを削除
rm include/button_handler.h
rm include/buzzer_controller.h
rm include/config.h
rm include/pomodoro_timer_app.h
rm include/timer_types.h
rm include/ui_renderer.h
rm src/button_handler.cpp
rm src/buzzer_controller.cpp
rm src/config.cpp
rm src/pomodoro_timer_app.cpp
rm src/ui_renderer.cpp

# 変更をコミット
git add .
git commit -m "Revert to original main.cpp"
```

## リファクタリング前のコミット情報

- **コミットハッシュ**: `f8513cf`
- **コミットメッセージ**: "Initial plan"
- **日付**: 2025-10-20

## リファクタリング後のコミット情報

- **最初のコミット**: `4fc47ba` - "Refactor main.cpp into modular structure"
- **ドキュメント追加**: `aff5972` - "Add architecture documentation"
- **修正**: `de9728d` - "Fix line counts in documentation"
- **サマリー追加**: `6d3f247` - "Add comprehensive refactoring summary"

## トラブルシューティング

### ビルドエラーが発生する場合

1. **インクルードパスの確認**
   - PlatformIOは自動的に`include/`ディレクトリを認識します
   - 手動でinclude pathを追加する必要はありません

2. **依存関係の確認**
   ```bash
   pio lib list
   ```

3. **クリーンビルド**
   ```bash
   pio run --target clean
   pio run
   ```

### 実行時エラーが発生する場合

1. **元のコードと比較**
   ```bash
   git diff f8513cf 6d3f247 -- src/
   ```

2. **特定のモジュールのみ復元**
   - 問題のあるモジュールのみを元に戻す
   - 他のモジュールはそのまま使用

3. **デバッグ出力の確認**
   - シリアルモニターで`[DBG]`出力を確認
   - どのモジュールで問題が発生しているか特定

## サポート

問題が解決しない場合：

1. GitHubのイシューを作成
2. エラーメッセージとスタックトレースを含める
3. 実行したコマンドを記載
4. 期待される動作と実際の動作を説明

## 重要な注意事項

- ロールバック前にバックアップを取ることを推奨
- 実機でテスト済みの状態に戻すことを推奨
- リファクタリングの変更は機能的には同等のはず
- 動作確認が取れるまで、元のブランチを残しておくことを推奨

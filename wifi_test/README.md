# Wi-Fi 接続テストスケッチ

このフォルダには Seeed XIAO ESP32-C3 向けの Wi-Fi 接続テスト用 Arduino スケッチが含まれています。スケッチを Arduino IDE で開き、SSID / パスワードを設定して書き込むことで、Wi-Fi への接続状況を確認できます。

## 使い方

1. `wifi_connection_test.ino` を Arduino IDE で開く。
2. ファイル先頭付近の `WIFI_SSID` と `WIFI_PASSWORD` を接続したいネットワークに合わせて書き換える。
3. ボード設定を "Seeed XIAO ESP32C3" にして書き込む。
4. シリアルモニタ (115200 bps) を開き、接続状態・IP アドレス・RSSI・HTTP リクエスト結果などを確認する。

## 出力内容

- 接続試行の進捗と結果
- 接続に成功した場合の IP アドレスや RSSI (電波強度)
- 失敗した場合のステータスコード（認証失敗、タイムアウトなど）
- HTTP テストが成功したかどうか

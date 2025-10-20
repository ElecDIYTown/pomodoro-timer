/*
 * 設定実装 - ポモドーロタイマー v0
 * Wi-Fi設定とデバッグ関数
 */

#include "config.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Wi-Fi / NTP 設定
// -----------------------------------------------------------------------------
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char *NTP_SERVER_1 = "ntp.nict.jp";
const char *NTP_SERVER_2 = "time.cloudflare.com";
const char *NTP_SERVER_3 = "pool.ntp.org";

#if PT_ENABLE_DEBUG
void logWifiCredentialConstants()
{
  DBG_PRINTLN("[DBG] WiFi credential constants:");
  DBG_PRINT("[DBG] WIFI_SSID_VALUE: '");
  DBG_PRINT(WIFI_SSID_VALUE);
  DBG_PRINTLN("'");
  DBG_PRINTF("[DBG] WIFI_SSID_VALUE length: %u\n", static_cast<unsigned>(strlen(WIFI_SSID_VALUE)));
  DBG_PRINTF("[DBG] WIFI_PASSWORD_VALUE length: %u\n", static_cast<unsigned>(strlen(WIFI_PASSWORD_VALUE)));
}
#endif

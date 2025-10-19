/*
 * Wi-Fi Connection Diagnostic Sketch for Seeed XIAO ESP32-C3
 *
 * What this sketch does:
 *   - Tries to connect to the configured Wi-Fi network
 *   - Prints detailed status information and diagnostics to the Serial Monitor
 *   - Reports IP address, RSSI, and failure reasons
 *   - Performs optional HTTP connectivity test
 *   - Repeats connectivity checks periodically so you can observe stability
 */

#include <Arduino.h>
#include <WiFi.h>

// -----------------------------------------------------------------------------
// User configuration
// -----------------------------------------------------------------------------
// TODO: update these with your Wi-Fi credentials before uploading.
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Optional: set to false if you do not want to run the HTTP reachability check.
const bool ENABLE_HTTP_TEST = true;

// -----------------------------------------------------------------------------
// Diagnostic settings
// -----------------------------------------------------------------------------
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000; // 20 seconds
constexpr uint32_t WIFI_RETRY_DELAY_MS = 5000;      // wait before retrying
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 5000; // periodic status log
constexpr uint32_t HTTP_TEST_INTERVAL_MS = 30000;   // run HTTP test every 30s

// HTTP test target
const char *HTTP_TEST_HOST = "httpstat.us";
const uint16_t HTTP_TEST_PORT = 80;
const char *HTTP_TEST_PATH = "/200"; // returns HTTP 200 OK

// -----------------------------------------------------------------------------
// Utility helpers
// -----------------------------------------------------------------------------
static void printDivider()
{
  Serial.println(F("----------------------------------------"));
}

static void printWifiStatus()
{
  wl_status_t status = WiFi.status();
  Serial.print(F("[STATUS] WiFi.status() = "));
  switch (status)
  {
  case WL_CONNECTED:
    Serial.println(F("WL_CONNECTED"));
    break;
  case WL_DISCONNECTED:
    Serial.println(F("WL_DISCONNECTED"));
    break;
  case WL_IDLE_STATUS:
    Serial.println(F("WL_IDLE_STATUS"));
    break;
  case WL_CONNECTION_LOST:
    Serial.println(F("WL_CONNECTION_LOST"));
    break;
  case WL_CONNECT_FAILED:
    Serial.println(F("WL_CONNECT_FAILED (wrong password?)"));
    break;
  case WL_NO_SHIELD:
    Serial.println(F("WL_NO_SHIELD"));
    break;
  case WL_NO_SSID_AVAIL:
    Serial.println(F("WL_NO_SSID_AVAIL"));
    break;
  default:
    Serial.print(F("Unknown ("));
    Serial.print(static_cast<int>(status));
    Serial.println(F(")"));
    break;
  }

  if (status == WL_CONNECTED)
  {
    Serial.print(F("[INFO] Connected to: "));
    Serial.println(WiFi.SSID());

    Serial.print(F("[INFO] IP address: "));
    Serial.println(WiFi.localIP());

    Serial.print(F("[INFO] RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));

    Serial.print(F("[INFO] MAC: "));
    Serial.println(WiFi.macAddress());

    Serial.print(F("[INFO] Gateway: "));
    Serial.println(WiFi.gatewayIP());

    Serial.print(F("[INFO] DNS: "));
    Serial.println(WiFi.dnsIP());
  }
}

static void printScanResults()
{
  Serial.println(F("[SCAN] Scanning for nearby networks..."));
  int n = WiFi.scanNetworks();
  if (n <= 0)
  {
    Serial.println(F("[SCAN] No networks found."));
    return;
  }

  for (int i = 0; i < n; ++i)
  {
    Serial.print(F("  #"));
    Serial.print(i + 1);
    Serial.print(F(" SSID="));
    Serial.print(WiFi.SSID(i));
    Serial.print(F(" RSSI="));
    Serial.print(WiFi.RSSI(i));
    Serial.print(F(" dBm Security="));
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    Serial.println(auth == WIFI_AUTH_OPEN ? F("OPEN") : F("SECURED"));
    delay(10);
  }
}

static bool attemptWifiConnection()
{
  printDivider();
  Serial.print(F("[ACTION] Connecting to SSID: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS)
  {
    Serial.print('.');
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("[RESULT] Wi-Fi connection successful!"));
    return true;
  }

  Serial.println(F("[RESULT] Wi-Fi connection failed."));
  printWifiStatus();
  return false;
}

static void runHttpTest()
{
  if (!ENABLE_HTTP_TEST || WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  Serial.print(F("[HTTP] Connecting to "));
  Serial.print(HTTP_TEST_HOST);
  Serial.print(':');
  Serial.println(HTTP_TEST_PORT);

  WiFiClient client;
  if (!client.connect(HTTP_TEST_HOST, HTTP_TEST_PORT))
  {
    Serial.println(F("[HTTP] Connection failed"));
    return;
  }

  client.print(F("GET "));
  client.print(HTTP_TEST_PATH);
  client.println(F(" HTTP/1.1"));
  client.print(F("Host: "));
  client.println(HTTP_TEST_HOST);
  client.println(F("Connection: close"));
  client.println();

  uint32_t start = millis();
  while (!client.available() && (millis() - start) < 3000)
  {
    delay(50);
  }

  if (client.available())
  {
    String line = client.readStringUntil('\n');
    Serial.print(F("[HTTP] First line: "));
    Serial.println(line);
  }
  else
  {
    Serial.println(F("[HTTP] No response within timeout"));
  }

  client.stop();
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(SERIAL_BAUD);
  while (!Serial)
  {
    delay(10);
  }

  printDivider();
  Serial.println(F("Wi-Fi Connection Diagnostic"));
  printDivider();

  printScanResults();

  bool connected = attemptWifiConnection();
  printWifiStatus();

  if (!connected)
  {
    Serial.println(F("[NOTE] Will retry periodically."));
  }
}

void loop()
{
  static uint32_t last_status_ms = 0;
  static uint32_t last_http_ms = 0;

  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED && (now % WIFI_RETRY_DELAY_MS) < 50)
  {
    attemptWifiConnection();
  }

  if (now - last_status_ms >= STATUS_PRINT_INTERVAL_MS)
  {
    printWifiStatus();
    last_status_ms = now;
  }

  if (now - last_http_ms >= HTTP_TEST_INTERVAL_MS)
  {
    runHttpTest();
    last_http_ms = now;
  }

  delay(100);
}

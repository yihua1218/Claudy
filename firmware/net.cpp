#include "net.h"
#include "state.h"
#include "display.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>

extern AppState g_state;
extern uint8_t clampDisplayBrightness(int value);
extern void applyBrightness(uint8_t value);
extern void markActivity(bool restoreBrightness);

static WebServer server(HTTP_PORT);
static bool s_connected = false;
static bool s_rebootPending = false;
static bool s_bootloaderPending = false;
static uint32_t s_rebootAtMs = 0;

static bool authOk() {
  if (strlen(AUTH_TOKEN) == 0) return true;
  if (!server.hasHeader("X-Claudy-Token")) return false;
  return server.header("X-Claudy-Token") == AUTH_TOKEN;
}

static void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

static void handleState() {
  if (!authOk()) { server.send(401, "text/plain", "auth"); return; }

  String body = server.arg("plain");
  if (body.length() == 0) { server.send(400, "text/plain", "empty body"); return; }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, "text/plain", err.c_str()); return; }

  const char* st   = doc["state"]   | "";
  const char* tool = doc["tool"]    | "";
  const char* msg  = doc["message"] | "";
  const char* client = doc["client"] | "";
  const char* model = doc["model"] | "";
  uint32_t used    = doc["tokens"]["used"] | 0;
  uint32_t maxv    = doc["tokens"]["max"]  | 0;

  g_state.state = parseStateName(st);
  g_state.tool  = parseToolName(tool);
  copyStr(g_state.message, sizeof(g_state.message), msg);
  if (client && *client) {
    copyStr(g_state.client, sizeof(g_state.client), client);
  }
  if (model && *model) {
    copyStr(g_state.model, sizeof(g_state.model), model);
  }
  if (maxv > 0) {
    g_state.tokensUsed = used;
    g_state.tokensMax  = maxv;
  }
  g_state.lastUpdateMs = millis();
  markActivity(true);
  requestRedraw();

  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGet() {
  StaticJsonDocument<256> doc;
  doc["state"]   = stateName(g_state.state);
  doc["tool"]    = toolName(g_state.tool);
  doc["message"] = g_state.message;
  doc["client"]  = g_state.client;
  doc["model"]   = g_state.model;
  doc["touch"]["present"] = g_state.touchPresent;
  doc["touch"]["locked"]  = g_state.touchLocked;
  doc["ui"]["view"]       = g_state.uiView;
  doc["ui"]["brightness"] = g_state.brightness;
  doc["ui"]["display_brightness"] = g_state.displayBrightness;
  doc["power"]["enabled"]       = g_state.powerSaveEnabled;
  doc["power"]["dimmed"]        = g_state.powerSaveDimmed;
  doc["power"]["timeout_ms"]    = g_state.powerSaveTimeoutMs;
  doc["power"]["brightness"]    = g_state.powerSaveBrightness;
  doc["tokens"]["used"] = g_state.tokensUsed;
  doc["tokens"]["max"]  = g_state.tokensMax;
  doc["uptime_ms"]      = millis();
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePowerGet() {
  StaticJsonDocument<256> doc;
  doc["enabled"] = g_state.powerSaveEnabled;
  doc["dimmed"] = g_state.powerSaveDimmed;
  doc["timeout_ms"] = g_state.powerSaveTimeoutMs;
  doc["brightness"] = g_state.powerSaveBrightness;
  doc["active_brightness"] = g_state.brightness;
  doc["display_brightness"] = g_state.displayBrightness;
  doc["last_activity_ms"] = g_state.lastActivityMs;
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePowerPost() {
  if (!authOk()) { server.send(401, "text/plain", "auth"); return; }

  String body = server.arg("plain");
  if (body.length() == 0) { server.send(400, "text/plain", "empty body"); return; }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, "text/plain", err.c_str()); return; }

  if (doc["enabled"].is<bool>()) {
    g_state.powerSaveEnabled = doc["enabled"].as<bool>();
  }
  if (doc["timeout_ms"].is<uint32_t>()) {
    g_state.powerSaveTimeoutMs = doc["timeout_ms"].as<uint32_t>();
  }
  if (doc["brightness"].is<int>()) {
    g_state.powerSaveBrightness = clampDisplayBrightness(doc["brightness"].as<int>());
  }
  if (doc["active_brightness"].is<int>()) {
    g_state.brightness = clampDisplayBrightness(doc["active_brightness"].as<int>());
  }

  markActivity(true);

  StaticJsonDocument<256> outDoc;
  outDoc["ok"] = true;
  outDoc["enabled"] = g_state.powerSaveEnabled;
  outDoc["dimmed"] = g_state.powerSaveDimmed;
  outDoc["timeout_ms"] = g_state.powerSaveTimeoutMs;
  outDoc["brightness"] = g_state.powerSaveBrightness;
  outDoc["active_brightness"] = g_state.brightness;
  outDoc["display_brightness"] = g_state.displayBrightness;
  String out;
  serializeJson(outDoc, out);
  server.send(200, "application/json", out);
}

static void put16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

static void put32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

static void handleScreenshot() {
  if (!canvas.getBuffer()) {
    server.send(503, "text/plain", "canvas not ready");
    return;
  }

  renderFrame();

  const int32_t w = canvas.width();
  const int32_t h = canvas.height();
  const uint32_t rowSize = ((uint32_t)w * 3 + 3) & ~3U;
  const uint32_t pixelBytes = rowSize * h;
  const uint32_t fileSize = 54 + pixelBytes;

  uint8_t header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  put32(header + 2, fileSize);
  put32(header + 10, 54);
  put32(header + 14, 40);                // BITMAPINFOHEADER
  put32(header + 18, w);
  put32(header + 22, h);                 // positive = bottom-up rows
  put16(header + 26, 1);
  put16(header + 28, 24);                // BGR888
  put32(header + 34, pixelBytes);
  put32(header + 38, 2835);              // 72 DPI
  put32(header + 42, 2835);

  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: image/bmp\r\n");
  client.print("Content-Disposition: inline; filename=claudy.bmp\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.printf("Content-Length: %lu\r\n", (unsigned long)fileSize);
  client.print("Connection: close\r\n\r\n");
  client.write(header, sizeof(header));

  uint8_t row[3 * 320 + 3];
  for (int32_t y = h - 1; y >= 0; --y) {
    uint32_t i = 0;
    for (int32_t x = 0; x < w; ++x) {
      uint16_t c = canvas.readPixelValue(x, y);
      row[i++] = (uint8_t)((c & 0x001F) * 255 / 31);          // B
      row[i++] = (uint8_t)(((c >> 5) & 0x003F) * 255 / 63);   // G
      row[i++] = (uint8_t)(((c >> 11) & 0x001F) * 255 / 31);  // R
    }
    while (i < rowSize) row[i++] = 0;
    client.write(row, rowSize);
    delay(0);
  }
}

static void scheduleReboot(bool bootloader) {
  s_rebootPending = true;
  s_bootloaderPending = bootloader;
  s_rebootAtMs = millis() + 500;
  server.send(200, "application/json", bootloader
    ? "{\"ok\":true,\"mode\":\"bootloader\",\"delay_ms\":500}"
    : "{\"ok\":true,\"mode\":\"reboot\",\"delay_ms\":500}");
}

static void handleReboot() {
  if (!authOk()) { server.send(401, "text/plain", "auth"); return; }
  scheduleReboot(false);
}

static void handleBootloader() {
  if (!authOk()) { server.send(401, "text/plain", "auth"); return; }
  scheduleReboot(true);
}

static void handleRoot() {
  String html = "<!doctype html><meta charset=utf-8><title>Claudy</title>"
                "<style>body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:24px;max-width:600px;margin:auto}"
                "code{background:#222;padding:2px 6px;border-radius:4px}</style>"
                "<h1>Claudy</h1><p>State: <b>";
  html += stateName(g_state.state);
  html += "</b></p><p>Tool: ";
  html += toolName(g_state.tool);
  html += "</p><p>Client: ";
  html += g_state.client;
  html += "</p><p>Model: ";
  html += g_state.model;
  html += "</p><p>Message: ";
  html += g_state.message;
  html += "</p><p><a href=\"/screenshot.bmp\">screenshot.bmp</a></p>";
  html += "<p>Power save: ";
  html += g_state.powerSaveEnabled ? "on" : "off";
  html += g_state.powerSaveDimmed ? " (dimmed)" : "";
  html += "</p>";
  html += "<p>Control: <code>POST /reboot</code>, <code>POST /bootloader</code></p>";
  html += "</p><pre>curl -X POST http://";
  html += MDNS_HOSTNAME;
  html += ".local/state -H 'Content-Type: application/json' \\\n  -d '{\"state\":\"thinking\",\"message\":\"hello\"}'</pre>";
  server.send(200, "text/html", html);
}

bool netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("WiFi: connecting to %s\n", WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: connect failed");
    s_connected = false;
    return false;
  }
  // Disable modem sleep AFTER connection so we don't fight the AP association.
  // Bursty POSTs were hitting 380+ms RTT due to WIFI_PS_MIN_MODEM beacon sleep.
  WiFi.setSleep(false);
  Serial.printf("WiFi: connected, IP=%s\n", WiFi.localIP().toString().c_str());

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("mDNS: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("mDNS: failed to start");
  }

  const char* hdrKeys[] = {"X-Claudy-Token"};
  server.collectHeaders(hdrKeys, 1);
  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/state",  HTTP_GET,  handleGet);
  server.on("/state",  HTTP_POST, handleState);
  server.on("/power",  HTTP_GET,  handlePowerGet);
  server.on("/power",  HTTP_POST, handlePowerPost);
  server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/bootloader", HTTP_POST, handleBootloader);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.printf("HTTP: listening on :%d\n", HTTP_PORT);

  s_connected = true;
  return true;
}

void netLoop() {
  server.handleClient();
  if (s_rebootPending && (int32_t)(millis() - s_rebootAtMs) >= 0) {
    if (s_bootloaderPending) {
      REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    }
    delay(20);
    esp_restart();
  }
}

bool netIsConnected() {
  return s_connected && WiFi.status() == WL_CONNECTED;
}

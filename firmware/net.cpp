#include "net.h"
#include "state.h"
#include "display.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

extern AppState g_state;

static WebServer server(HTTP_PORT);
static bool s_connected = false;

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
  uint32_t used    = doc["tokens"]["used"] | 0;
  uint32_t maxv    = doc["tokens"]["max"]  | 0;

  g_state.state = parseStateName(st);
  g_state.tool  = parseToolName(tool);
  copyStr(g_state.message, sizeof(g_state.message), msg);
  if (client && *client) {
    copyStr(g_state.client, sizeof(g_state.client), client);
  }
  if (maxv > 0) {
    g_state.tokensUsed = used;
    g_state.tokensMax  = maxv;
  }
  g_state.lastUpdateMs = millis();
  requestRedraw();

  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGet() {
  StaticJsonDocument<256> doc;
  doc["state"]   = stateName(g_state.state);
  doc["tool"]    = toolName(g_state.tool);
  doc["message"] = g_state.message;
  doc["client"]  = g_state.client;
  doc["tokens"]["used"] = g_state.tokensUsed;
  doc["tokens"]["max"]  = g_state.tokensMax;
  doc["uptime_ms"]      = millis();
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
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
  html += "</p><p>Message: ";
  html += g_state.message;
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
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.printf("HTTP: listening on :%d\n", HTTP_PORT);

  s_connected = true;
  return true;
}

void netLoop() {
  server.handleClient();
}

bool netIsConnected() {
  return s_connected && WiFi.status() == WL_CONNECTED;
}

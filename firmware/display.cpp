#include "display.h"
#include "state.h"
#include "mascot.h"
#include "config.h"

#ifndef ENABLE_CODEX_UI
#define ENABLE_CODEX_UI 0
#endif

#if ENABLE_CODEX_UI
#include "touch.h"

#include <WiFi.h>
#endif

LGFX tft;
lgfx::LGFX_Sprite canvas(&tft);

extern AppState g_state;
static bool s_dirty = true;

LGFX::LGFX() {
  { // 8-bit parallel bus (i80)
    auto cfg = _bus.config();
    cfg.freq_write = 20000000;   // 20MHz is the safe default for T-Display-S3 parallel wiring; 40MHz caused pixel corruption
    cfg.pin_wr = 8;
    cfg.pin_rd = 9;
    cfg.pin_rs = 7;   // D/C
    cfg.pin_d0 = 39;
    cfg.pin_d1 = 40;
    cfg.pin_d2 = 41;
    cfg.pin_d3 = 42;
    cfg.pin_d4 = 45;
    cfg.pin_d5 = 46;
    cfg.pin_d6 = 47;
    cfg.pin_d7 = 48;
    _bus.config(cfg);
    _panel.setBus(&_bus);
  }
  { // ST7789 panel
    auto cfg = _panel.config();
    cfg.pin_cs   = 6;
    cfg.pin_rst  = 5;
    cfg.pin_busy = -1;
    cfg.panel_width      = 170;
    cfg.panel_height     = 320;
    cfg.offset_x         = 35;
    cfg.offset_y         = 0;
    cfg.offset_rotation  = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits  = 1;
    cfg.readable         = false;
    cfg.invert           = true;
    cfg.rgb_order        = false;
    cfg.dlen_16bit       = false;
    cfg.bus_shared       = false;
    _panel.config(cfg);
  }
  { // Backlight PWM
    auto cfg = _light.config();
    cfg.pin_bl      = 38;
    cfg.invert      = false;
    cfg.freq        = 12000;
    cfg.pwm_channel = 7;
    _light.config(cfg);
    _panel.setLight(&_light);
  }
  setPanel(&_panel);
}

void displayBegin(uint8_t brightness) {
  // T-Display-S3 needs PIN 15 HIGH to power the LCD on USB-only operation.
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);

  tft.init();
  tft.setRotation(1);             // landscape: 320 wide, 170 tall
  tft.setBrightness(brightness);
  tft.fillScreen(TFT_BLACK);

  // Allocate offscreen sprite in DRAM (108KB; we have ~262KB free).
  // Earlier attempt to force PSRAM caused boot failures, likely because
  // pushSprite DMA on this board prefers internal memory.
  canvas.setColorDepth(16);
  if (!canvas.createSprite(tft.width(), tft.height())) {
    Serial.println("Sprite alloc FAILED");
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed", 4, 4);
  }
  canvas.fillScreen(TFT_BLACK);
  s_dirty = true;
}

void requestRedraw() { s_dirty = true; }

static void drawTokenBar(int x, int y, int w, int h, uint32_t used, uint32_t maxv) {
  canvas.drawRoundRect(x, y, w, h, 3, 0x4A49);
  if (maxv == 0) return;
  uint32_t fill = (uint64_t)(w - 4) * used / maxv;
  if (fill > (uint32_t)(w - 4)) fill = w - 4;
  uint16_t color = 0x06DF;             // teal-ish
  if (used * 100 / maxv > 75) color = 0xFD20;   // orange
  if (used * 100 / maxv > 90) color = 0xF800;   // red
  canvas.fillRoundRect(x + 2, y + 2, fill, h - 4, 2, color);
}

static void drawToolBadge(int x, int y, int w, int h, ToolIcon t) {
  if (t == TOOL_NONE) return;
  const char* glyph = "?";
  uint16_t bg = 0x39E7;
  switch (t) {
    case TOOL_READ:  glyph = "R"; bg = 0x3475; break;
    case TOOL_EDIT:  glyph = "E"; bg = 0xC9A0; break;
    case TOOL_WRITE: glyph = "W"; bg = 0xCAA0; break;
    case TOOL_BASH:  glyph = "$"; bg = 0x39E7; break;
    case TOOL_GREP:  glyph = "G"; bg = 0x4A89; break;
    case TOOL_WEB:   glyph = "@"; bg = 0x2C9F; break;
    case TOOL_TASK:  glyph = "T"; bg = 0x880F; break;
    default:         glyph = "*"; bg = 0x528A; break;
  }
  canvas.fillRoundRect(x, y, w, h, 4, bg);
  canvas.setTextColor(TFT_WHITE, bg);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::Font2);
  canvas.drawString(glyph, x + w / 2, y + h / 2);
}

#if ENABLE_CODEX_UI
static bool isCodexClient() {
  return strstr(g_state.client, "codex") != nullptr;
}

static bool isVSCodeCodex() {
  return strstr(g_state.client, "vscode") != nullptr;
}

static uint16_t clientAccent() {
  if (isVSCodeCodex()) return 0x04BF;   // VS Code-ish blue
  if (isCodexClient()) return 0x05FF;   // cyan
  return 0xCC2D;                        // Claude orange
}

static void drawCodexMotif(int x, int y, int w, int h) {
  if (!isCodexClient()) return;
  uint16_t c = clientAccent();
  const int l = 18;
  canvas.drawLine(x, y, x + l, y, c);
  canvas.drawLine(x, y, x, y + l, c);
  canvas.drawLine(x + w - l, y, x + w, y, c);
  canvas.drawLine(x + w, y, x + w, y + l, c);
  canvas.drawLine(x, y + h - l, x, y + h, c);
  canvas.drawLine(x, y + h, x + l, y + h, c);
  canvas.drawLine(x + w - l, y + h, x + w, y + h, c);
  canvas.drawLine(x + w, y + h - l, x + w, y + h, c);

  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(c, TFT_BLACK);
  canvas.drawString(">_", x + 6, y + 5);
}

static void drawClientBadge(int right, int y) {
  const char* label = isVSCodeCodex() ? "CODEX" : (isCodexClient() ? "CX" : "CL");
  uint16_t c = clientAccent();
  int w = isVSCodeCodex() ? 50 : 28;
  int x = right - w;
  canvas.fillRoundRect(x, y, w, 15, 4, c);
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE, c);
  canvas.drawString(label, x + w / 2, y + 7);
}

static void drawTouchPill(int x, int y) {
  uint16_t bg = g_state.touchLocked ? 0x7BEF : (g_state.touchPresent ? clientAccent() : 0x3186);
  canvas.fillRoundRect(x, y, 36, 14, 4, bg);
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE, bg);
  canvas.drawString(g_state.touchLocked ? "LOCK" : (g_state.touchPresent ? "TAP" : "NO T"), x + 18, y + 7);
}

static void drawSmallLine(const char* label, const char* value, int x, int y, int w) {
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(0x8C71, TFT_BLACK);
  canvas.drawString(label, x, y);
  canvas.setTextColor(0xDFFF, TFT_BLACK);
  char buf[54];
  strncpy(buf, value ? value : "", sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  while (canvas.textWidth(buf) > w - 42 && strlen(buf) > 4) {
    size_t n = strlen(buf);
    buf[n - 1] = 0;
  }
  canvas.drawString(buf, x + 42, y);
}
#endif

void renderFrame() {
  bool animating = mascotAnimating(g_state.state);
  if (!s_dirty && !animating) return;
  s_dirty = false;

  canvas.fillScreen(TFT_BLACK);

  // Layout — finalized in mockup/index.html, exported 2026-05-13.
  const int PAD       = 15;
  const int MASCOT_W  = 119;
  const int GAP       = 12;
  const int STATE_Y   = 2;
  const int CHIP_Y    = 34;
  const int CHIP_W    = 25;
  const int CHIP_H    = 17;
  const int MSG_GAP   = 6;
  const int BAR_H     = 13;
  const int BAR_YOFF  = -16;

  const int W = tft.width();
  const int H = tft.height();

  // Mascot region (left)
  const int lx = PAD;
  const int ly = PAD;
  const int lw = MASCOT_W;
  const int lh = H - 2 * PAD;
#if ENABLE_CODEX_UI
  drawCodexMotif(lx - 3, ly - 3, lw + 6, lh + 6);
#endif
  drawMascot(canvas, g_state.state, lx, ly, lw, lh);
#if ENABLE_CODEX_UI
  drawTouchPill(lx, H - PAD - 14);
#endif

  // Right column
  const int tx = lx + lw + GAP;
  const int ty = PAD;
  const int tw = W - tx - PAD;
  const int th = H - 2 * PAD;

  // State name
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.drawString(stateName(g_state.state), tx, ty + STATE_Y);
#if ENABLE_CODEX_UI
  drawClientBadge(W - PAD, ty + 4);
#endif

  // Tool chip + label
  int cursorY = ty + CHIP_Y;
  if (g_state.tool != TOOL_NONE) {
    drawToolBadge(tx, cursorY, CHIP_W, CHIP_H, g_state.tool);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(0xBDF7, TFT_BLACK);
    canvas.setTextDatum(middle_left);
    canvas.drawString(toolName(g_state.tool), tx + CHIP_W + 6, cursorY + CHIP_H / 2);
    cursorY += CHIP_H + MSG_GAP;
  } else {
    cursorY += MSG_GAP;
  }

  // Message (up to 2 lines). efontTW_16 = Traditional Chinese variant with
  // broader symbol/punctuation coverage than efontCN. Width-based wrapping
  // that respects UTF-8 boundaries (since CJK has no inter-word spaces).
  canvas.setFont(&fonts::efontTW_16);
  canvas.setTextColor(0xBDF7, TFT_BLACK);
  canvas.setTextDatum(top_left);
#if ENABLE_CODEX_UI
  if (g_state.uiView == 1) {
    drawSmallLine("client", g_state.client, tx, cursorY, tw);
    drawSmallLine("model", g_state.model[0] ? g_state.model : "-", tx, cursorY + 17, tw);
    drawSmallLine("ip", WiFi.localIP().toString().c_str(), tx, cursorY + 34, tw);
    drawSmallLine("touch", touchConfigName(), tx, cursorY + 51, tw);
  } else if (g_state.uiView == 2) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(0xBDF7, TFT_BLACK);
    canvas.drawString("tap mascot: view", tx, cursorY);
    canvas.drawString("tap text: pin 30s", tx, cursorY + 16);
    canvas.drawString("swipe up/down: light", tx, cursorY + 32);
    canvas.drawString("hold: lock touch", tx, cursorY + 48);
  } else {
#endif
    char buf[80];
    strncpy(buf, g_state.message, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    const int lineH = 18;

    int len = (int)strlen(buf);
    int pos = 0;
    int y = cursorY;
    for (int line = 0; line < 2 && pos < len; line++) {
      // Walk forward one UTF-8 codepoint at a time, tracking the last fit
      // and the last space-break opportunity.
      int end = pos;
      int last_space = -1;
      while (end < len) {
        int next = end + 1;
        while (next < len && ((unsigned char)buf[next] & 0xC0) == 0x80) next++;
        char saved = buf[next];
        buf[next] = 0;
        int w = canvas.textWidth(buf + pos);
        buf[next] = saved;
        if (w > tw) break;
        if (buf[end] == ' ') last_space = end;
        end = next;
      }
      int break_at = end;
      // If we ran out of width mid-word, prefer breaking at the last space.
      if (end < len && last_space > pos) break_at = last_space;
      // Guarantee progress.
      if (break_at == pos) {
        int next = pos + 1;
        while (next < len && ((unsigned char)buf[next] & 0xC0) == 0x80) next++;
        break_at = next;
      }
      char saved = buf[break_at];
      buf[break_at] = 0;
      canvas.drawString(buf + pos, tx, y);
      buf[break_at] = saved;
      pos = break_at;
      while (pos < len && buf[pos] == ' ') pos++;   // skip leading spaces
      y += lineH;
    }
#if ENABLE_CODEX_UI
  }

  if (millis() < g_state.uiHoldUntilMs && g_state.touchHint[0]) {
    uint16_t c = g_state.touchLocked ? 0x7BEF : clientAccent();
    int hintW = canvas.textWidth(g_state.touchHint) + 12;
    if (hintW < 54) hintW = 54;
    if (hintW > tw) hintW = tw;
    int hx = tx;
    int hy = ty + th - 44;
    canvas.fillRoundRect(hx, hy, hintW, 17, 5, c);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_WHITE, c);
    canvas.drawString(g_state.touchHint, hx + hintW / 2, hy + 8);
  }
#endif

  // Token area
  if (g_state.tokensMax > 0) {
    int pct = (int)((uint64_t)g_state.tokensUsed * 100 / g_state.tokensMax);
    if (pct > 999) pct = 999;
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);

    const int pctY = ty + th;

    // Big percentage, right-aligned at bottom-right
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(bottom_right);
    canvas.drawString(pctStr, tx + tw, pctY);
    const int pctW = canvas.textWidth(pctStr);

    // Detail label above the bar
    char tb[24];
    if (g_state.tokensMax >= 1000000) {
      snprintf(tb, sizeof(tb), "%lu.%luk / 1M",
               (unsigned long)(g_state.tokensUsed / 1000),
               (unsigned long)((g_state.tokensUsed % 1000) / 100));
    } else {
      snprintf(tb, sizeof(tb), "%luk / %luk",
               (unsigned long)(g_state.tokensUsed / 1000),
               (unsigned long)(g_state.tokensMax / 1000));
    }
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(0x8C71, TFT_BLACK);
    canvas.setTextDatum(bottom_left);
    canvas.drawString(tb, tx, pctY - 22);

    // Bar to the left of the % number
    const int by = pctY + BAR_YOFF;
    int bw = tw - pctW - 8;
    if (bw < 8) bw = 8;
    drawTokenBar(tx, by, bw, BAR_H, g_state.tokensUsed, g_state.tokensMax);
  }

  canvas.pushSprite(0, 0);
}

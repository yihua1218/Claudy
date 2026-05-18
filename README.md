# Claudy

[繁體中文版](README.zh-TW.md)

A Claude Code status mascot for the **LilyGo T-Display-S3** (ESP32-S3, 1.9" 170×320 ST7789).

Claude Code hook events on your Mac are pushed via HTTP to the ESP32, which renders a procedurally-drawn Claude mascot that reacts to what Claude is doing — Idle / Thinking / Working / Waiting / Error / Done — plus a context-token progress bar.

```
+----------------------------------+
|          Working                 |
|  (face) [E] Edit                 |
|         firmware/firmware.ino    |
|         [██████░░░░] 85k / 200k  |
+----------------------------------+
```

## Architecture

```
Claude Code (Mac)
   └─ hook event JSON on stdin
        └─ bridge/send-state.sh   (backgrounded, returns instantly)
             └─ bridge/send_state.py
                  └─ POST http://claudy.local/state
                       └─ ESP32 WebServer
                            └─ Display render
```

The bridge is fire-and-forget with a 1.5s timeout — it never blocks Claude.

## Hardware

- LilyGo T-Display-S3 (ESP32-S3 with 8-bit parallel ST7789, 16MB flash + PSRAM)
- USB-C cable

## One-time setup (Mac)

```bash
cd ~/Developer/Claudy

# 1. Toolchain
./scripts/setup.sh                              # installs arduino-cli, ESP32 core, libs

# 2. WiFi credentials
cp firmware/config.h.example firmware/config.h
$EDITOR firmware/config.h                       # set WIFI_SSID + WIFI_PASSWORD

# 3. Build & flash
./scripts/build.sh
./scripts/flash.sh                              # auto-detects /dev/cu.usbmodem*

# 4. Watch first boot
./scripts/monitor.sh
# You should see "WiFi: connected, IP=..." and "mDNS: http://claudy.local/"

# 5. Verify from your Mac
curl http://claudy.local/state                   # GET — current device state as JSON
curl http://claudy.local/screenshot.bmp -o claudy.bmp
./scripts/test-state.sh                          # cycle through all states on the display

# 6. Install Claude Code hooks
./bridge/install-hooks.sh                        # adds entries alongside existing hooks
# Restart Claude Code so it re-reads settings.json.
```

The mascot should now react in real time to anything you do in Claude Code.

## How the states map

| Claude Code event                              | Mascot state | Notes                                       |
|------------------------------------------------|--------------|---------------------------------------------|
| `SessionStart`                                 | Idle         |                                             |
| `UserPromptSubmit`                             | Thinking     | Message = first chars of your prompt        |
| `PreToolUse`                                   | Working      | Tool badge + brief input (file/cmd/pattern) |
| `PostToolUse`                                  | Thinking     | Returns to thinking between tools           |
| `PostToolUseFailure`                           | Error        | Brief flash before next event               |
| `Notification` / `Permission*` / `Elicitation` | Waiting      | "Approve X?" prompts                        |
| `Stop` / `TaskCompleted`                       | Done → Idle  | Bridge auto-fades after 3s                  |
| `SessionEnd`                                   | Idle         |                                             |

## Testing without a device

See [No-Device Simulation Options](docs/no-device-simulation.md) for the browser mock display, remote-host setup, and screenshot guidance.

```bash
echo '{"hook_event_name":"PreToolUse","tool_name":"Bash","tool_input":{"command":"ls"}}' \
  | python3 bridge/send_state.py
```

POST will silently fail (no device on the network) but the mapping path is exercised.

## Configuration

### Firmware (`firmware/config.h`)

| Define            | Default  | Meaning                                                                                    |
|-------------------|----------|--------------------------------------------------------------------------------------------|
| `WIFI_SSID`       | –        | Your WiFi SSID                                                                             |
| `WIFI_PASSWORD`   | –        | Your WiFi password                                                                         |
| `MDNS_HOSTNAME`   | `claudy` | mDNS hostname (device reachable at `<hostname>.local`)                                     |
| `HTTP_PORT`       | `80`     |                                                                                            |
| `AUTH_TOKEN`      | `""`     | Optional. If set, requests must send `X-Claudy-Token: <value>`                             |
| `BRIGHTNESS`      | `200`    | 0–255                                                                                      |
| `ENABLE_TOUCH`    | `0`      | Optional CST816 touch support. Enable only after confirming the touch pins for your board. |
| `ENABLE_CODEX_UI` | `1`      | Shows Codex/client-specific visual details and touch help views.                           |
| `IDLE_TIMEOUT_MS` | `60000`  | Auto-return to Idle after this long with no updates                                        |

### Bridge (environment variables)

| Var                 | Default                           |
|---------------------|-----------------------------------|
| `CLAUDY_URL`        | `http://claudy.local/state`       |
| `CLAUDY_TOKEN`      | empty                             |
| `CLAUDY_MAX_TOKENS` | `200000` (context budget for bar) |

To set these for hooks, export them in `~/.zshenv` so they're inherited by every shell Claude Code spawns.

## HTTP API

### `POST /state`

```json
{
  "state":   "idle | thinking | working | waiting | error | done",
  "tool":    "Bash | Read | Edit | Write | Grep | Glob | WebFetch | Task | …",
  "message": "Any short text",
  "tokens":  { "used": 12345, "max": 200000 }
}
```

All fields optional except `state`. Returns `{"ok":true}`.

### `GET /state`

Returns current state + uptime + IP.

### `GET /screenshot.bmp`

Downloads the current rendered frame as a 24-bit BMP. See [Claudy State Screenshot Gallery](docs/state-screenshot-gallery.md) for neutral example captures.

### `GET /`

HTML status page.

## Touch Interactions

Touch support is optional because T-Display-S3 variants may expose different touch wiring. Once `ENABLE_TOUCH` is enabled and the CST816 controller is detected:

- Tap the mascot area to trigger a short local "boop" animation.
- Tap the text area to pin the current display for 30 seconds.
- Swipe left or right to switch between status, client info, and touch help views.
- Swipe up or down to adjust brightness.
- Long-press to lock or unlock touch input.

Future approve-from-screen support should use a waiting-only view with a deliberate confirmation gesture, such as long-press then tap, so normal mascot taps can never approve a tool by accident.

## Troubleshooting

**No serial port at flash time**
Plug in via USB-C. If still missing: hold **BOOT**, tap **RESET**, release **BOOT**, then retry. Check `ls /dev/cu.*` — T-Display-S3 enumerates as `/dev/cu.usbmodem*`.

**Compile error: PSRAM not found**
Make sure you're flashing the 16MB/PSRAM variant. The FQBN in `scripts/build.sh` already sets `PSRAM=opi,FlashSize=16M`.

**Mascot stuck on Boot / "WiFi failed"**
SSID/password wrong in `config.h`, or your WiFi is 5GHz-only (ESP32-S3 is 2.4GHz only).

**`claudy.local` doesn't resolve**
mDNS sometimes takes 5–10s after boot. Try the device IP instead: `curl http://<claudy-ip>/state`. Set `CLAUDY_URL` to `http://<claudy-ip>/state` if mDNS is flaky on your network.

**Hooks not firing**
Restart Claude Code after `install-hooks.sh`. Then run a tool — you should see the mascot react. To debug, run `bridge/send_state.py` manually with a sample event.

## Removing

```bash
./bridge/uninstall-hooks.sh    # removes Claudy entries, leaves other hooks intact
```

## Layout

```
firmware/         Arduino sketch — LovyanGFX driver, mascot, web server
  firmware.ino    main
  display.{h,cpp} LGFX panel config + rendering pipeline
  mascot.{h,cpp}  procedural face drawing
  net.{h,cpp}     WiFi + mDNS + WebServer
  state.h         enums + parsers
  config.h        WiFi creds (gitignored)
  sketch.yaml     Arduino CLI profile

bridge/           Mac → ESP32 hook bridge
  send-state.sh   bash wrapper (called by hooks; backgrounds the python)
  send_state.py   event → state mapping + POST
  install-hooks.sh
  uninstall-hooks.sh

scripts/          Dev workflow
  setup.sh        install arduino-cli + ESP32 core + libs
  build.sh        compile firmware
  flash.sh        upload to device
  monitor.sh      open serial monitor
  test-state.sh   POST every state to verify rendering
```

# No-Device Simulation Options

This document covers how to test Claudy before the ESP32-S3 board arrives.

## Option 1: Browser Mock Display

The fastest path is the built-in mock server:

```bash
./scripts/mock-server.py
```

Then open:

```text
http://127.0.0.1:8765/
```

Point Claudy bridge traffic at the mock API:

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
./scripts/test-state.sh
```

For VS Code Remote-SSH Codex, run the mock server on the same remote Linux host where Codex hooks execute, then launch or restart Codex with:

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
./bridge/install-codex-hooks.sh
```

The mock server implements:

- `POST /state`
- `GET /state`
- `GET /` as a browser display

This is enough to validate hook mapping, state transitions, `client`, tool labels, and token bars without hardware.

### Taking Screenshots

Recent ESP32-S3 firmware exposes a best-effort framebuffer screenshot endpoint:

```bash
curl http://<claudy-ip>/screenshot.bmp -o claudy.bmp
```

This downloads the current offscreen render sprite as a 24-bit BMP. It is useful for quick debugging when the device is online.

Neutral example captures for all states are available in [Claudy State Screenshot Gallery](state-screenshot-gallery.md).

For repeatable documentation screenshots, reproduce the same state in the browser mock display and capture the browser page. The mock flow is easier to automate and does not depend on camera angle, glare, WiFi timing, or the physical LCD.

Start the mock server:

```bash
./scripts/mock-server.py
```

Send the state you want to capture:

```bash
curl -X POST http://127.0.0.1:8765/state \
  -H 'Content-Type: application/json' \
  -d '{"state":"working","tool":"Bash","message":"screenshot test","client":"codex-vscode","tokens":{"used":42000,"max":200000}}'
```

Then open and capture:

```text
http://127.0.0.1:8765/
```

Use your browser or OS screenshot tool for the image. If you need to capture what the physical LCD actually looks like, use a camera; the BMP endpoint captures the rendered sprite, not optical LCD characteristics such as brightness, viewing angle, or glare.

## Option 2: Browser on Another Machine

Run the mock server on the remote Linux host and bind to all interfaces:

```bash
./scripts/mock-server.py --host 0.0.0.0 --port 8765
```

Then open this from Windows:

```text
http://<remote-linux-ip>:8765/
```

Set:

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
```

Codex hooks can still POST locally on the remote host, while your Windows browser watches the display over the network.

## Option 3: Raspberry Pi HDMI CLI Display

A Raspberry Pi can implement the same API and render to a small HDMI display without X-Window. The cleanest approach is:

- Run an HTTP server compatible with Claudy's `/state` API.
- Store the latest state in memory.
- Render directly to the Linux framebuffer, usually `/dev/fb0`.
- Use a fixed layout scaled to the current framebuffer resolution.

Possible rendering backends:

- Python + Pillow drawing into an RGB image, then write converted pixels to `/dev/fb0`.
- Python + `pygame` using the framebuffer console backend where available.
- A small C or Rust service using framebuffer ioctls for better performance.

Recommended API compatibility:

```json
{
  "state": "idle | thinking | working | waiting | error | done",
  "tool": "Bash | Read | Edit | Write | Grep | Web | Task | Tool",
  "message": "short text",
  "client": "codex-vscode-remote",
  "tokens": { "used": 42000, "max": 200000 }
}
```

The Pi display should also provide `GET /state`, so the bridge, mock browser, ESP32 firmware, and Pi renderer all share one mental model.

## Different Screen Sizes

Keep the layout resolution-independent:

- Divide the screen into a mascot region and an info region.
- Compute padding, font size, and token bar dimensions from the framebuffer width and height.
- Keep the same state colors and client visual identity across sizes.
- For very small screens, prioritize state, mascot/client identity, tool, and one message line.
- For larger HDMI screens, add token details, event name, model, and last-update timestamp.

This lets the same `/state` API drive:

- ESP32-S3 320x170 landscape display.
- Browser mock display.
- Raspberry Pi HDMI framebuffer.
- Future small SPI or e-paper displays.

## Recommended Development Flow

1. Start with `scripts/mock-server.py` in the Remote-SSH Linux environment.
2. Install Codex hooks with `bridge/install-codex-hooks.sh`.
3. Confirm live updates in the browser.
4. Build the Raspberry Pi renderer against the same `/state` contract only if you need a physical always-on display before the ESP32-S3 arrives.
5. When the ESP32-S3 arrives, switch `CLAUDY_URL` from the mock or Pi endpoint to `http://<claudy-ip>/state`.

#!/usr/bin/env python3
"""A no-device Claudy HTTP API and browser display simulator."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import html
import json
import time

STATE = {
    "state": "idle",
    "tool": "",
    "message": "Mock display ready",
    "client": "mock",
    "event": "",
    "tokens": {"used": 0, "max": 0},
    "updated_at": time.time(),
}


def page():
    state = html.escape(str(STATE.get("state", "")))
    tool = html.escape(str(STATE.get("tool", "")))
    message = html.escape(str(STATE.get("message", "")))
    client = html.escape(str(STATE.get("client", "")))
    event = html.escape(str(STATE.get("event", "")))
    tokens = STATE.get("tokens") or {}
    used = int(tokens.get("used") or 0)
    maxv = int(tokens.get("max") or 0)
    pct = min(999, int(used * 100 / maxv)) if maxv else 0
    fill = min(100, pct)
    return f"""<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Claudy Mock Display</title>
<style>
  :root {{
    color-scheme: dark;
    --bg: #101113;
    --panel: #050607;
    --text: #f3f5f7;
    --muted: #9ba8b5;
    --accent: #4cc9f0;
    --work: #22c55e;
    --wait: #f59e0b;
    --error: #ef4444;
    --done: #14b8a6;
  }}
  body {{
    margin: 0;
    min-height: 100vh;
    display: grid;
    place-items: center;
    background: #181b1f;
    font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    color: var(--text);
  }}
  main {{
    width: min(94vw, 760px);
    display: grid;
    gap: 18px;
  }}
  .device {{
    aspect-ratio: 320 / 170;
    width: min(94vw, 640px);
    background: var(--panel);
    border: 8px solid #242930;
    border-radius: 18px;
    box-shadow: 0 20px 60px #0008;
    display: grid;
    grid-template-columns: 44% 1fr;
    padding: 5%;
    box-sizing: border-box;
  }}
  .mascot {{
    display: grid;
    place-items: center;
    color: var(--accent);
    font: 900 clamp(54px, 18vw, 122px) / 1 ui-monospace, SFMono-Regular, Consolas, monospace;
  }}
  .mascot::before {{
    content: "<>";
  }}
  .state-working .mascot {{ color: var(--work); }}
  .state-waiting .mascot {{ color: var(--wait); }}
  .state-error .mascot {{ color: var(--error); }}
  .state-done .mascot {{ color: var(--done); }}
  .info {{
    min-width: 0;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
  }}
  h1 {{
    margin: 0;
    font-size: clamp(24px, 7vw, 58px);
    line-height: 1;
  }}
  .meta, .message, .client {{
    color: var(--muted);
    overflow-wrap: anywhere;
  }}
  .message {{
    min-height: 2.4em;
    font-size: clamp(14px, 3.2vw, 24px);
  }}
  .bar {{
    height: 16px;
    border: 1px solid #4b5563;
    border-radius: 5px;
    overflow: hidden;
  }}
  .bar > div {{
    height: 100%;
    width: {fill}%;
    background: var(--accent);
  }}
  pre {{
    margin: 0;
    padding: 14px;
    border-radius: 8px;
    background: #0f1115;
    color: #d8dee9;
    overflow: auto;
  }}
</style>
<main>
  <section class="device state-{state}">
    <div class="mascot" aria-hidden="true"></div>
    <div class="info">
      <div>
        <h1>{state.title()}</h1>
        <div class="meta">{tool or "No tool"} · {event or "No event"}</div>
      </div>
      <div class="message">{message}</div>
      <div>
        <div class="client">{client} · {used} / {maxv} tokens · {pct}%</div>
        <div class="bar"><div></div></div>
      </div>
    </div>
  </section>
  <pre>export CLAUDY_URL=http://127.0.0.1:8765/state
./scripts/test-state.sh</pre>
</main>
<script>setTimeout(() => location.reload(), 1000);</script>
"""


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/state":
            self._send_json(200, STATE)
            return
        if self.path != "/":
            self.send_error(404)
            return
        body = page().encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/state":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0") or 0)
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError as exc:
            self._send_json(400, {"ok": False, "error": str(exc)})
            return

        for key in ("state", "tool", "message", "client", "event", "model"):
            if key in payload:
                STATE[key] = payload[key]
        if "tokens" in payload:
            STATE["tokens"] = payload["tokens"]
        STATE["updated_at"] = time.time()
        self._send_json(200, {"ok": True})

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Claudy mock display: http://{args.host}:{args.port}/")
    print(f"API endpoint: http://{args.host}:{args.port}/state")
    server.serve_forever()


if __name__ == "__main__":
    main()

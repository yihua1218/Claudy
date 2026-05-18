#!/bin/bash
# Adds Claudy entries to Codex hooks.json on the machine where Codex runs.
#
# For Windows VS Code + Remote-SSH, run this script on the remote Linux host.
# It writes ~/.codex/hooks.json by default. Override with CODEX_HOOKS.

set -euo pipefail

SETTINGS="${CODEX_HOOKS:-$HOME/.codex/hooks.json}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BRIDGE_CMD="$HERE/send-codex-state.sh"
CLIENT="${CLAUDY_CLIENT:-codex-vscode-remote}"
URL="${CLAUDY_URL:-}"
TOKEN="${CLAUDY_TOKEN:-}"
MAX_TOKENS="${CLAUDY_MAX_TOKENS:-}"

mkdir -p "$(dirname "$SETTINGS")"
if [ ! -f "$SETTINGS" ]; then
  printf '{\n  "hooks": {}\n}\n' > "$SETTINGS"
fi
if [ ! -x "$BRIDGE_CMD" ]; then
  chmod +x "$BRIDGE_CMD"
fi

BACKUP="$SETTINGS.bak.$(date +%Y%m%d%H%M%S)"
cp "$SETTINGS" "$BACKUP"
echo "Backed up $SETTINGS -> $BACKUP"

python3 - "$SETTINGS" "$BRIDGE_CMD" "$CLIENT" "$URL" "$TOKEN" "$MAX_TOKENS" <<'PY'
import json
import pathlib
import shlex
import sys

settings_path = pathlib.Path(sys.argv[1])
bridge_cmd = sys.argv[2]
client = sys.argv[3]
url = sys.argv[4]
token = sys.argv[5]
max_tokens = sys.argv[6]

events = [
    "SessionStart",
    "UserPromptSubmit",
    "PreToolUse",
    "PermissionRequest",
    "PostToolUse",
    "Stop",
]

try:
    data = json.loads(settings_path.read_text() or "{}")
except json.JSONDecodeError as exc:
    raise SystemExit(f"{settings_path} is not valid JSON: {exc}") from exc

hooks = data.setdefault("hooks", {})
env = {"CLAUDY_CLIENT": client}
if url:
    env["CLAUDY_URL"] = url
if token:
    env["CLAUDY_TOKEN"] = token
if max_tokens:
    env["CLAUDY_MAX_TOKENS"] = max_tokens
command = " ".join(f"{key}={shlex.quote(value)}" for key, value in env.items())
command = f"{command} {shlex.quote(bridge_cmd)}"
added = 0

for event in events:
    entries = hooks.setdefault(event, [])
    target = None
    for entry in entries:
        if entry.get("matcher", "") == "":
            target = entry
            break
    if target is None:
        target = {"matcher": "", "hooks": []}
        entries.append(target)

    target_hooks = target.setdefault("hooks", [])
    already = any(
        bridge_cmd in h.get("command", "") or "send-codex-state.sh" in h.get("command", "")
        for h in target_hooks
    )
    if not already:
        target_hooks.append({"type": "command", "command": command})
        added += 1

settings_path.write_text(json.dumps(data, indent=2) + "\n")
print(f"Added {added} Codex hook entries across {len(events)} events.")
PY

echo "Done. In Codex, review/trust hooks if prompted, or run /hooks."
echo "For no-board testing: export CLAUDY_URL=http://127.0.0.1:8765/state before launching Codex."

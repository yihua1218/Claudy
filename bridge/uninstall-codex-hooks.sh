#!/bin/bash
# Removes Claudy Codex hook entries from hooks.json.

set -euo pipefail

SETTINGS="${CODEX_HOOKS:-$HOME/.codex/hooks.json}"
if [ ! -f "$SETTINGS" ]; then
  echo "Codex hooks file not found at $SETTINGS"
  exit 0
fi

BACKUP="$SETTINGS.bak.$(date +%Y%m%d%H%M%S)"
cp "$SETTINGS" "$BACKUP"
echo "Backed up $SETTINGS -> $BACKUP"

python3 - "$SETTINGS" <<'PY'
import json
import pathlib
import sys

settings_path = pathlib.Path(sys.argv[1])
data = json.loads(settings_path.read_text() or "{}")
hooks = data.get("hooks", {})
removed = 0

for entries in hooks.values():
    if not isinstance(entries, list):
        continue
    for entry in entries:
        target_hooks = entry.get("hooks", [])
        keep = []
        for hook in target_hooks:
            command = hook.get("command", "")
            if "send-codex-state.sh" in command or "send_codex_state.py" in command:
                removed += 1
            else:
                keep.append(hook)
        entry["hooks"] = keep

settings_path.write_text(json.dumps(data, indent=2) + "\n")
print(f"Removed {removed} Claudy Codex hook entries.")
PY

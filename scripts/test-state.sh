#!/bin/bash
# Cycle through all states to verify the display rendering on the device.
set -euo pipefail

RAW_URL="${CLAUDY_URL:-http://claudy.local/state}"
AUTH="${CLAUDY_TOKEN:-}"
IP_CACHE="${CLAUDY_IP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/claudy/ip_cache}"
LEGACY_IP_CACHE="$HOME/.claude/claudy_ip_cache"
CLIENT="${CLAUDY_CLIENT:-manual-test}"

# Reuse the IP cache from send_state.py so we don't depend on flaky system mDNS.
resolve_url() {
  local url="$1"
  local cache="$IP_CACHE"
  case "$url" in
    *".local"*)
      if [ ! -f "$cache" ] && [ -f "$LEGACY_IP_CACHE" ]; then
        cache="$LEGACY_IP_CACHE"
      fi
      if [ -f "$cache" ]; then
        local mtime
        mtime=$(stat -c %Y "$cache" 2>/dev/null || stat -f %m "$cache" 2>/dev/null || echo 0)
        if [ $(( $(date +%s) - mtime )) -lt 60 ]; then
          local ip
          ip=$(cat "$cache")
          if [ -n "$ip" ]; then
            echo "$url" | sed "s|://[^:/]*|://$ip|"
            return
          fi
        fi
      fi
      ;;
  esac
  echo "$url"
}

URL="$(resolve_url "$RAW_URL")"

send() {
  local payload="$1"
  if [ -n "$AUTH" ]; then
    curl -sS --max-time 2 -H "Content-Type: application/json" -H "X-Claudy-Token: $AUTH" -X POST -d "$payload" "$URL" || true
  else
    curl -sS --max-time 2 -H "Content-Type: application/json" -X POST -d "$payload" "$URL" || true
  fi
  echo
}

echo "==> POST $URL"
send "{\"state\":\"idle\",\"message\":\"Hello from test\",\"client\":\"$CLIENT\"}"; sleep 2
send "{\"state\":\"thinking\",\"message\":\"Reading some files...\",\"client\":\"$CLIENT\"}"; sleep 2
send "{\"state\":\"working\",\"tool\":\"Bash\",\"message\":\"npm run build\",\"client\":\"$CLIENT\",\"tokens\":{\"used\":42000,\"max\":200000}}"; sleep 2
send "{\"state\":\"working\",\"tool\":\"Edit\",\"message\":\"firmware/firmware.ino\",\"client\":\"$CLIENT\",\"tokens\":{\"used\":85000,\"max\":200000}}"; sleep 2
send "{\"state\":\"working\",\"tool\":\"Grep\",\"message\":\"pattern: TODO\",\"client\":\"$CLIENT\",\"tokens\":{\"used\":110000,\"max\":200000}}"; sleep 2
send "{\"state\":\"waiting\",\"message\":\"Approve Bash command?\",\"client\":\"$CLIENT\",\"tokens\":{\"used\":150000,\"max\":200000}}"; sleep 2
send "{\"state\":\"error\",\"message\":\"Compilation failed\",\"client\":\"$CLIENT\",\"tokens\":{\"used\":180000,\"max\":200000}}"; sleep 2
send "{\"state\":\"done\",\"message\":\"Task complete!\",\"client\":\"$CLIENT\"}"; sleep 2
send "{\"state\":\"idle\",\"message\":\"Idle\",\"client\":\"$CLIENT\"}"

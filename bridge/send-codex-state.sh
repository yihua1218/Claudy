#!/bin/bash
# Claudy Codex hook bridge. Intended for VS Code Remote-SSH on Linux:
# Codex runs the hook on the remote Linux host, then this script POSTs to Claudy.

set -euo pipefail

INPUT=$(cat)
HERE="$(cd "$(dirname "$0")" && pwd)"

(
  export CLAUDY_CLIENT="${CLAUDY_CLIENT:-codex-vscode-remote}"
  printf '%s' "$INPUT" | /usr/bin/env python3 "$HERE/send_codex_state.py" >/dev/null 2>&1
) &
disown
exit 0

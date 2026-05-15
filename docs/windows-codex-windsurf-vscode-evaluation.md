# Windows Codex, Windsurf, and VS Code Support Evaluation

Last reviewed: 2026-05-15

## Summary

Claudy is already close to supporting Codex on Windows because the ESP32-S3 firmware exposes a simple HTTP `/state` API and does not care which desktop agent produced the event. Most of the work is on the host side: cross-platform setup scripts, Windows-safe hook commands, event-schema adapters, and a small firmware extension if we want different on-device artwork for VS Code Codex and Windsurf.

Recommended direction: keep the firmware API generic, split the bridge into a normalizer plus per-agent adapters, and add a `client` or `theme` field so the display can select different mascot variants without forking the whole rendering pipeline.

## Current Project Fit

The current implementation assumes macOS and Claude Code in several places:

- `bridge/install-hooks.sh` edits `~/.claude/settings.json`.
- `bridge/send-state.sh` requires Bash and backgrounds `python3`.
- `bridge/send_state.py` uses `dns-sd` for `.local` mDNS pre-resolution and stores its cache under `~/.claude/claudy_ip_cache`.
- `scripts/setup.sh` uses Homebrew.
- `scripts/flash.sh` and `scripts/monitor.sh` auto-detect `/dev/cu.usbmodem*` and `/dev/cu.usbserial-*`, not Windows `COM*` ports.
- `scripts/test-state.sh` uses macOS `stat -f`.

The firmware side is more portable:

- `firmware/net.cpp` accepts JSON over HTTP and only parses `state`, `tool`, `message`, and `tokens`.
- `firmware/display.cpp` and `firmware/mascot.cpp` render from the normalized `AppState`.
- No firmware logic depends on Claude Code specifically.

## Verified Platform Notes

OpenAI documents the Codex IDE extension as working with VS Code forks including Cursor and Windsurf, with Codex IDE integrations available on macOS, Windows, and Linux. On Windows, Codex can run natively with the Windows sandbox or through WSL2 when a Linux-native environment is needed.

Codex hooks are configured in `hooks.json` or inline `[hooks]` tables beside active Codex config layers. The practical user/project locations are `~/.codex/hooks.json`, `~/.codex/config.toml`, `<repo>/.codex/hooks.json`, and `<repo>/.codex/config.toml`. Current Codex hook events include `SessionStart`, `PreToolUse`, `PermissionRequest`, `PostToolUse`, `UserPromptSubmit`, and `Stop`.

Windsurf Cascade hooks are a separate integration surface. User-level Windsurf IDE hooks live at `~/.codeium/windsurf/hooks.json`; Windows system-level hooks live at `C:\ProgramData\Windsurf\hooks.json`. Windsurf hook entries support `command` for macOS/Linux and `powershell` for Windows. Their event names and input schema differ from Codex: examples include `pre_read_code`, `post_write_code`, `pre_run_command`, `post_run_command`, `pre_user_prompt`, and `post_cascade_response_with_transcript`.

## Support Strategy

### 1. Make the Bridge Agent-Neutral

Refactor `bridge/send_state.py` into three layers:

- `transport`: POST to the ESP32, auth header, timeout, mDNS/IP fallback.
- `normalizer`: converts one agent event into Claudy payload `{state, tool, message, tokens, client}`.
- `adapters`: Claude Code, Codex, and Windsurf/Cascade schema readers.

Suggested files:

- `bridge/claudy_bridge.py`: shared transport and payload helpers.
- `bridge/adapters/claude.py`: current Claude Code mapping.
- `bridge/adapters/codex.py`: Codex hook mapping.
- `bridge/adapters/windsurf.py`: Windsurf Cascade hook mapping.
- `bridge/send_state.py`: backward-compatible default entrypoint.
- `bridge/send_codex_state.py`: Codex entrypoint.
- `bridge/send_windsurf_state.py`: Windsurf Cascade entrypoint.

This keeps the firmware stable and makes future IDE support mostly additive.

### 2. Add Windows Wrappers

Add PowerShell wrappers instead of depending on Git Bash:

- `bridge/send-state.ps1`
- `bridge/install-codex-hooks.ps1`
- `bridge/install-windsurf-hooks.ps1`
- `bridge/uninstall-codex-hooks.ps1`
- `scripts/setup.ps1`
- `scripts/flash.ps1`
- `scripts/monitor.ps1`
- `scripts/test-state.ps1`

The PowerShell hook wrapper should read stdin, launch Python in the background, and return immediately, preserving the current fire-and-forget behavior.

Example shape:

```powershell
$inputJson = [Console]::In.ReadToEnd()
$script = Join-Path $PSScriptRoot "send_codex_state.py"
Start-Process -WindowStyle Hidden -FilePath "py" -ArgumentList @("-3", $script) `
  -RedirectStandardInput "<temp-json-file>"
exit 0
```

In practice, using a temporary JSON file is safer than trying to pipe stdin into `Start-Process`.

### 3. Codex Hook Mapping

Codex hook input is close enough to the current Claude Code mapping that the same state model works:

| Codex event         | Claudy state          | Notes                                                                 |
|---------------------|-----------------------|-----------------------------------------------------------------------|
| `SessionStart`      | `idle`                | Message can show `startup`, `resume`, or `clear`.                     |
| `UserPromptSubmit`  | `thinking`            | Use `prompt`.                                                         |
| `PreToolUse`        | `working`             | Use `tool_name` and `tool_input`.                                     |
| `PermissionRequest` | `waiting`             | Show approval request.                                                |
| `PostToolUse`       | `thinking` or `error` | If tool response indicates failure, show `error`; otherwise thinking. |
| `Stop`              | `done -> idle`        | Same 3-second fade as current bridge.                                 |

Important Codex-specific adjustments:

- Normalize `tool_name: "apply_patch"` to `Edit` or `Write` for display purposes.
- Treat MCP tool names such as `mcp__server__tool` as `Task` or `Tool`, unless a better mapping is added later.
- Keep transcript token parsing best-effort because Codex warns that transcript format is not a stable hook interface.
- Add `CLAUDY_CLIENT=codex-vscode` or `CLAUDY_CLIENT=codex-windsurf` when installing hooks from a specific IDE.

Suggested user-level Codex hook file on Windows:

```json
{
  "hooks": {
    "SessionStart": [{ "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }],
    "PreToolUse": [{ "matcher": "", "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }],
    "PermissionRequest": [{ "matcher": "", "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }],
    "PostToolUse": [{ "matcher": "", "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }],
    "Stop": [{ "hooks": [{ "type": "command", "command": "py -3 C:\\path\\to\\Claudy\\bridge\\send_codex_state.py" }] }]
  }
}
```

Codex users may need to review and trust hooks through `/hooks` before non-managed hooks run.

### 4. Windsurf Cascade Mapping

There are two Windsurf paths:

- Codex IDE extension inside Windsurf: use the Codex hook adapter above.
- Windsurf Cascade native agent: use Windsurf Cascade hooks and a separate adapter.

Cascade event mapping:

| Windsurf event                          | Claudy state           | Notes                                                         |
|-----------------------------------------|------------------------|---------------------------------------------------------------|
| `pre_user_prompt`                       | `thinking`             | Use `tool_info.user_prompt`.                                  |
| `pre_read_code` / `post_read_code`      | `working` / `thinking` | Tool `Read`; message file path.                               |
| `pre_write_code` / `post_write_code`    | `working` / `thinking` | Tool `Edit`; message file path.                               |
| `pre_run_command`                       | `working`              | Tool `Bash`; message command line.                            |
| `post_run_command`                      | `thinking` or `error`  | Use exit/result if present.                                   |
| `post_cascade_response`                 | `done`                 | Fade back to idle.                                            |
| `post_cascade_response_with_transcript` | `done`                 | Can parse transcript later for tokens, but treat as unstable. |

Windsurf config can include both `command` and `powershell` in one file. For Windows support, the installer should write `powershell`.

## Windows Toolchain Work

Firmware build/flash should support these Windows paths:

- Native PowerShell:
  - Install Arduino CLI with `winget` if available.
  - Run `arduino-cli config add board_manager.additional_urls ...`.
  - Install `esp32:esp32`, `LovyanGFX`, and `ArduinoJson`.
  - Detect serial ports from `Get-CimInstance Win32_SerialPort` or ask the user for a `COM` port.
- WSL2:
  - Useful for repository work, but USB flashing is often easier from native Windows unless USB/IP is configured.
  - Keep native PowerShell scripts as the recommended flashing path.

The existing Bash scripts can remain for macOS/Linux.

## mDNS and Networking

Windows `.local` resolution is less predictable across networks. The bridge should:

- First try the configured `CLAUDY_URL`.
- If the host ends in `.local`, try normal Python DNS resolution before platform-specific tools.
- Allow `CLAUDY_URL=http://<device-ip>/state` as the recommended Windows fallback.
- Move the cache from `~/.claude/claudy_ip_cache` to a neutral path:
  - Windows: `%LOCALAPPDATA%\Claudy\ip_cache`
  - macOS/Linux: `~/.cache/claudy/ip_cache`, with backward-compatible read from the old path.

## Device Artwork and Client Identity

Yes, the ESP32-S3 can draw different graphics for Windsurf and VS Code Codex. The current mascot is procedural pixel art, so adding variants is inexpensive and fits the device.

Recommended API extension:

```json
{
  "state": "working",
  "tool": "Edit",
  "message": "firmware/display.cpp",
  "client": "codex-vscode",
  "theme": "vscode"
}
```

Firmware changes:

- Add `ClientKind` to `state.h`: `CLIENT_CLAUDE`, `CLIENT_CODEX_VSCODE`, `CLIENT_CODEX_WINDSURF`, `CLIENT_WINDSURF_CASCADE`, `CLIENT_UNKNOWN`.
- Add `client` storage to `AppState`.
- Parse `client` or `theme` in `net.cpp`.
- Update `drawMascot(...)` signature to accept the client/theme.
- Keep state animations shared, but switch body shape/accent details by client.

Suggested visual language:

- Claude Code: keep the current orange Claudy pixel mascot.
- VS Code Codex: blue/cyan angular monitor or bracket-shaped companion, with a small `<>` or split-panel motif.
- Windsurf Codex: teal/green wave or sail-like companion, with a flowing crest or curved pixel trail.
- Windsurf Cascade native: same Windsurf family, but add a stepped/cascade stripe to distinguish it from Codex-in-Windsurf.

Avoid relying on text labels alone. On a 170x320 display, shape and color will be more recognizable than tiny words.

## Risks

- Codex hook coverage is still not a complete enforcement boundary for every possible tool path. That is acceptable for Claudy because Claudy is telemetry/status display, not security enforcement.
- Codex transcript parsing is explicitly not stable, so token extraction should remain best-effort.
- Windsurf Cascade hooks and Codex hooks are different products/schemas even when both are used inside Windsurf.
- Windows PowerShell execution policy may block scripts on some machines.
- USB serial behavior varies by driver and board boot mode.
- mDNS may fail; IP configuration must be first-class.

## Proposed Implementation Phases

1. Refactor bridge into shared transport plus adapters while preserving the current Claude Code behavior.
2. Add Codex adapter and manual `.codex/hooks.json` examples for Windows, macOS, and Linux.
3. Add PowerShell wrappers and Windows setup/flash/test scripts.
4. Add `client`/`theme` to the HTTP payload and firmware state.
5. Add client-specific mascot variants.
6. Add Windsurf Cascade adapter and installer.
7. Update README files with Windows and IDE-specific setup paths.

## References

- OpenAI Codex IDE extension: https://developers.openai.com/codex/ide
- OpenAI Codex Windows guide: https://developers.openai.com/codex/windows
- OpenAI Codex hooks: https://developers.openai.com/codex/hooks
- OpenAI Codex app for Windows: https://developers.openai.com/codex/app/windows
- Windsurf Cascade hooks: https://docs.windsurf.com/windsurf/cascade/hooks

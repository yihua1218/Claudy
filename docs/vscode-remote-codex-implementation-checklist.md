# VS Code Remote-SSH Codex Implementation Checklist

Target setup: Windows VS Code, Remote-SSH into Linux, Codex running inside the remote VS Code workspace.

## Implementation

- [x] Confirm the active branch is `feature/windsurf-vscode-codex-support`.
- [x] Keep the existing Claude Code bridge path working.
- [x] Add a Codex-specific hook wrapper for the remote Linux host: `bridge/send-codex-state.sh`.
- [x] Add a Codex-specific Python entrypoint: `bridge/send_codex_state.py`.
- [x] Extend `bridge/send_state.py` so Codex hook events map to Claudy states.
- [x] Normalize Codex `apply_patch` to display as `Edit` or `Write`.
- [x] Normalize MCP tool names to display as `Task`.
- [x] Add `CLAUDY_CLIENT=codex-vscode-remote` support.
- [x] Move the preferred IP cache path to `~/.cache/claudy/ip_cache`, while still reading the legacy Claude path.
- [x] Add Linux-friendly `.local` fallback resolution using normal DNS, `avahi-resolve-host-name`, or `getent`.
- [x] Add a Codex hook installer for the remote Linux host: `bridge/install-codex-hooks.sh`.
- [x] Add a Codex hook uninstaller: `bridge/uninstall-codex-hooks.sh`.
- [x] Add firmware-side `client` storage and `/state` reporting.
- [x] Update `scripts/test-state.sh` so it works on Linux and sends a `client` field.
- [x] Add a no-device browser mock API server: `scripts/mock-server.py`.
- [ ] Add firmware artwork variants that visually distinguish `codex-vscode-remote`, Windsurf Codex, and Windsurf Cascade.
- [ ] Add a Windsurf Cascade adapter after the VS Code Codex path is stable.
- [ ] Add Windows-native PowerShell setup/flash scripts for users who do not use Remote-SSH.

## Remote Linux Setup

- [ ] On the remote Linux host, clone or update this branch.
- [ ] If using the real ESP32-S3, set `CLAUDY_URL` to the device URL, preferably an IP address if `.local` is unreliable.
- [ ] If using the mock display, run `./scripts/mock-server.py` and set `CLAUDY_URL=http://127.0.0.1:8765/state`.
- [ ] Run `./bridge/install-codex-hooks.sh` on the remote Linux host.
- [ ] In Codex, review/trust hooks if prompted, or run `/hooks`.
- [ ] Submit a prompt in Codex and confirm the mock display or ESP32-S3 receives updates.

## Verification

- [x] Python bridge supports importable `build_payload(...)` and source-specific `run(...)`.
- [x] Codex wrapper runs through the same mapping path as Claude Code.
- [x] Run `python3 -m py_compile bridge/send_state.py bridge/send_codex_state.py scripts/mock-server.py`.
- [x] Run the mock display and test with `CLAUDY_URL=http://127.0.0.1:8765/state ./scripts/test-state.sh`.
- [ ] Test from the actual VS Code Remote-SSH Codex environment.
- [ ] Test against the physical ESP32-S3 when the board arrives.

# VS Code Remote-SSH Codex 實作 Checklist

目標環境：Windows VS Code，透過 Remote-SSH 連到 Linux，並在遠端 VS Code workspace 裡使用 Codex。

## 實作項目

- [x] 確認目前 branch 是 `feature/windsurf-vscode-codex-support`。
- [x] 保留既有 Claude Code bridge 路徑。
- [x] 新增遠端 Linux 主機用的 Codex hook wrapper：`bridge/send-codex-state.sh`。
- [x] 新增 Codex 專用 Python entrypoint：`bridge/send_codex_state.py`。
- [x] 擴充 `bridge/send_state.py`，讓 Codex hook events 可轉成 Claudy states。
- [x] 將 Codex `apply_patch` 正規化為顯示用的 `Edit` 或 `Write`。
- [x] 將 MCP tool 名稱正規化為顯示用的 `Task`。
- [x] 加入 `CLAUDY_CLIENT=codex-vscode-remote` 支援。
- [x] 將新的 IP cache 預設位置移到 `~/.cache/claudy/ip_cache`，並保留讀取舊 Claude cache 的相容性。
- [x] 加入 Linux-friendly `.local` fallback resolution：一般 DNS、`avahi-resolve-host-name`、`getent`。
- [x] 新增遠端 Linux 主機用的 Codex hook installer：`bridge/install-codex-hooks.sh`。
- [x] 新增 Codex hook uninstaller：`bridge/uninstall-codex-hooks.sh`。
- [x] 在韌體端加入 `client` 儲存和 `/state` 回報。
- [x] 更新 `scripts/test-state.sh`，讓它可在 Linux 使用並送出 `client` 欄位。
- [x] 新增無板子時可用的瀏覽器 mock API server：`scripts/mock-server.py`。
- [ ] 加入韌體圖案變體，用視覺區分 `codex-vscode-remote`、Windsurf Codex、Windsurf Cascade。
- [ ] VS Code Codex 路徑穩定後，再加入 Windsurf Cascade adapter。
- [ ] 為不使用 Remote-SSH 的使用者加入 Windows-native PowerShell setup/flash scripts。

## 遠端 Linux 設定

- [ ] 在遠端 Linux 主機 clone 或更新這個 branch。
- [ ] 如果使用實體 ESP32-S3，設定 `CLAUDY_URL` 到裝置 URL；若 `.local` 不穩，建議直接用 IP。
- [ ] 如果使用 mock display，執行 `./scripts/mock-server.py`，並設定 `CLAUDY_URL=http://127.0.0.1:8765/state`。
- [ ] 在遠端 Linux 主機執行 `./bridge/install-codex-hooks.sh`。
- [ ] 在 Codex 裡依提示 review/trust hooks，或執行 `/hooks`。
- [ ] 在 Codex 送出 prompt，確認 mock display 或 ESP32-S3 收到更新。

## 驗證

- [x] Python bridge 支援可匯入的 `build_payload(...)` 和不同來源用的 `run(...)`。
- [x] Codex wrapper 會走和 Claude Code 相同的 mapping path。
- [x] 執行 `python3 -m py_compile bridge/send_state.py bridge/send_codex_state.py scripts/mock-server.py`。
- [x] 啟動 mock display，並用 `CLAUDY_URL=http://127.0.0.1:8765/state ./scripts/test-state.sh` 測試。
- [ ] 在實際 VS Code Remote-SSH Codex 環境測試。
- [ ] 板子到貨後，用實體 ESP32-S3 測試。

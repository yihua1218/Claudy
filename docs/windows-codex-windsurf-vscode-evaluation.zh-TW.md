# Windows Codex、Windsurf、VS Code 支援評估

最後檢視日期：2026-05-15

## 結論

Claudy 要支援 Windows 上的 Codex、VS Code、Windsurf，整體可行，而且韌體端改動不大。原因是 ESP32-S3 目前只接收通用的 HTTP `/state` JSON；真正綁定 macOS / Claude Code 的地方主要在 host 端 bridge、hook 安裝腳本、mDNS 處理，以及燒錄/監控腳本。

建議方向：保留韌體的通用 `/state` API，把 bridge 拆成「事件正規化」和「不同 agent 的 adapter」，並在 payload 增加 `client` 或 `theme` 欄位。這樣 VS Code Codex、Windsurf 裡的 Codex、Windsurf Cascade 都可以共用同一個顯示管線，ESP32-S3 也能根據來源畫不同圖案。

## 目前專案狀態

目前程式碼裡明顯假設 macOS 和 Claude Code：

- `bridge/install-hooks.sh` 修改 `~/.claude/settings.json`。
- `bridge/send-state.sh` 需要 Bash，並用背景程序啟動 `python3`。
- `bridge/send_state.py` 用 macOS 的 `dns-sd` 預解析 `.local`，快取放在 `~/.claude/claudy_ip_cache`。
- `scripts/setup.sh` 使用 Homebrew。
- `scripts/flash.sh` 和 `scripts/monitor.sh` 偵測 `/dev/cu.usbmodem*`、`/dev/cu.usbserial-*`，沒有處理 Windows `COM*`。
- `scripts/test-state.sh` 使用 macOS `stat -f`。

韌體端比較中立：

- `firmware/net.cpp` 只解析 `state`、`tool`、`message`、`tokens`。
- `firmware/display.cpp` 和 `firmware/mascot.cpp` 都根據正規化後的 `AppState` 繪製。
- 韌體邏輯本身不需要知道事件來自 Claude Code、Codex 或 Windsurf。

## 已確認的平台資訊

OpenAI 官方文件寫明 Codex IDE extension 可用於 VS Code forks，包含 Cursor 和 Windsurf；VS Code-compatible editors 與 JetBrains IDE 的 Codex IDE integrations 可在 macOS、Windows、Linux 使用。Windows 上可用原生 Windows sandbox 執行，也可在需要 Linux-native 環境時使用 WSL2。

Codex hooks 設定在 `hooks.json` 或 `config.toml` 內的 `[hooks]`。實務上常用位置是 `~/.codex/hooks.json`、`~/.codex/config.toml`、`<repo>/.codex/hooks.json`、`<repo>/.codex/config.toml`。目前 Codex hook 事件包含 `SessionStart`、`PreToolUse`、`PermissionRequest`、`PostToolUse`、`UserPromptSubmit`、`Stop`。

Windsurf 自家的 Cascade hooks 是另一套介面。Windsurf IDE 的 user-level hooks 位於 `~/.codeium/windsurf/hooks.json`；Windows system-level hooks 位於 `C:\ProgramData\Windsurf\hooks.json`。Windsurf hook 支援 `command` 給 macOS/Linux，也支援 `powershell` 給 Windows。它的事件名稱和輸入 schema 與 Codex 不同，例如 `pre_read_code`、`post_write_code`、`pre_run_command`、`post_run_command`、`pre_user_prompt`、`post_cascade_response_with_transcript`。

## 支援策略

### 1. 將 Bridge 改成 Agent-Neutral

建議把 `bridge/send_state.py` 拆成三層：

- `transport`：負責 POST 到 ESP32、auth header、timeout、mDNS/IP fallback。
- `normalizer`：把不同 agent 的事件轉成 Claudy payload：`{state, tool, message, tokens, client}`。
- `adapters`：分別處理 Claude Code、Codex、Windsurf/Cascade schema。

建議檔案：

- `bridge/claudy_bridge.py`：共用 transport 和 payload helpers。
- `bridge/adapters/claude.py`：目前 Claude Code mapping。
- `bridge/adapters/codex.py`：Codex hook mapping。
- `bridge/adapters/windsurf.py`：Windsurf Cascade hook mapping。
- `bridge/send_state.py`：保留相容目前用法。
- `bridge/send_codex_state.py`：Codex 入口。
- `bridge/send_windsurf_state.py`：Windsurf Cascade 入口。

這樣未來新增 IDE 或 agent 時，大多只需要新增 adapter，不需要動韌體。

### 2. 補 Windows PowerShell Wrappers

Windows 不應要求使用者一定要有 Git Bash。建議新增：

- `bridge/send-state.ps1`
- `bridge/install-codex-hooks.ps1`
- `bridge/install-windsurf-hooks.ps1`
- `bridge/uninstall-codex-hooks.ps1`
- `scripts/setup.ps1`
- `scripts/flash.ps1`
- `scripts/monitor.ps1`
- `scripts/test-state.ps1`

PowerShell hook wrapper 需要讀 stdin、背景啟動 Python、立即返回，維持目前 fire-and-forget 的行為。

實作上建議先把 stdin 寫到暫存 JSON 檔，再用 `Start-Process` 啟動 Python，會比嘗試直接 pipe 給背景程序穩定。

### 3. Codex Hook Mapping

Codex 的 hook input 和目前 Claude Code mapping 很接近，可以沿用同一組 Claudy 狀態：

| Codex event         | Claudy state          | 備註                                                        |
|---------------------|-----------------------|-------------------------------------------------------------|
| `SessionStart`      | `idle`                | 訊息可顯示 `startup`、`resume`、`clear`。                      |
| `UserPromptSubmit`  | `thinking`            | 使用 `prompt`。                                              |
| `PreToolUse`        | `working`             | 使用 `tool_name` 和 `tool_input`。                           |
| `PermissionRequest` | `waiting`             | 顯示待核准狀態。                                             |
| `PostToolUse`       | `thinking` 或 `error` | 如果 tool response 顯示失敗，顯示 `error`；否則回到 thinking。 |
| `Stop`              | `done -> idle`        | 沿用目前 3 秒後淡回 idle。                                   |

Codex 需要特別處理：

- 將 `tool_name: "apply_patch"` 正規化成顯示用的 `Edit` 或 `Write`。
- MCP tool 名稱如 `mcp__server__tool` 可先顯示成 `Task` 或 `Tool`。
- transcript token 解析只能 best-effort，因為 Codex 官方文件提醒 transcript 格式不是穩定 hook 介面。
- 從特定 IDE 安裝 hook 時設定 `CLAUDY_CLIENT=codex-vscode` 或 `CLAUDY_CLIENT=codex-windsurf`。

Codex 的 non-managed hooks 可能需要使用者透過 `/hooks` 檢視並信任後才會執行。

### 4. Windsurf Cascade Mapping

Windsurf 有兩種情境：

- 在 Windsurf 裡使用 Codex IDE extension：走 Codex adapter。
- 使用 Windsurf 原生 Cascade agent：走 Windsurf Cascade hooks 和獨立 adapter。

Cascade 建議 mapping：

| Windsurf event                          | Claudy state           | 備註                                         |
|-----------------------------------------|------------------------|----------------------------------------------|
| `pre_user_prompt`                       | `thinking`             | 使用 `tool_info.user_prompt`。                |
| `pre_read_code` / `post_read_code`      | `working` / `thinking` | Tool 顯示 `Read`，message 顯示檔案路徑。       |
| `pre_write_code` / `post_write_code`    | `working` / `thinking` | Tool 顯示 `Edit`，message 顯示檔案路徑。       |
| `pre_run_command`                       | `working`              | Tool 顯示 `Bash`，message 顯示 command line。  |
| `post_run_command`                      | `thinking` 或 `error`  | 若 event 有 exit/result 欄位，可用來判斷錯誤。 |
| `post_cascade_response`                 | `done`                 | 之後淡回 idle。                               |
| `post_cascade_response_with_transcript` | `done`                 | 未來可解析 token，但應視為不穩定格式。         |

Windsurf 的 `hooks.json` 可同時放 `command` 和 `powershell`。Windows 安裝器應寫入 `powershell` 欄位。

## Windows Toolchain 調整

韌體 build/flash 建議支援：

- 原生 PowerShell：
  - 用 `winget` 安裝 Arduino CLI。
  - 執行 `arduino-cli config add board_manager.additional_urls ...`。
  - 安裝 `esp32:esp32`、`LovyanGFX`、`ArduinoJson`。
  - 用 `Get-CimInstance Win32_SerialPort` 偵測序列埠，或請使用者輸入 `COM` port。
- WSL2：
  - 適合 repo 開發，但 USB 燒錄通常原生 Windows 比較省事，除非另外設定 USB/IP。
  - 建議把原生 PowerShell 燒錄腳本列為 Windows 預設路徑。

現有 Bash 腳本可繼續保留給 macOS/Linux。

## mDNS 與網路

Windows 對 `.local` 的解析在不同網路上不一定穩定。Bridge 應該：

- 先嘗試使用 `CLAUDY_URL`。
- 如果 host 是 `.local`，先用 Python 一般 DNS resolution，再嘗試平台特定 fallback。
- 明確支援 `CLAUDY_URL=http://<device-ip>/state`，並把它列為 Windows 推薦 fallback。
- 將快取從 `~/.claude/claudy_ip_cache` 移到中立位置：
  - Windows：`%LOCALAPPDATA%\Claudy\ip_cache`
  - macOS/Linux：`~/.cache/claudy/ip_cache`
  - 仍可向下相容讀取舊路徑。

## ESP32-S3 圖案能不能依 Windsurf / VS Code Codex 不同？

可以，而且很適合目前的架構。現有 mascot 是程序化 pixel art，不需要大型 bitmap asset；增加幾個小型 grid / palette / animation 變體就能區分來源。

建議擴充 HTTP payload：

```json
{
  "state": "working",
  "tool": "Edit",
  "message": "firmware/display.cpp",
  "client": "codex-vscode",
  "theme": "vscode"
}
```

韌體調整：

- 在 `state.h` 增加 `ClientKind`：`CLIENT_CLAUDE`、`CLIENT_CODEX_VSCODE`、`CLIENT_CODEX_WINDSURF`、`CLIENT_WINDSURF_CASCADE`、`CLIENT_UNKNOWN`。
- 在 `AppState` 儲存 client。
- 在 `net.cpp` 解析 `client` 或 `theme`。
- 將 `drawMascot(...)` 改成接收 client/theme。
- 狀態動畫共用，但根據 client/theme 切換外型和 accent。

建議視覺語言：

- Claude Code：保留目前橘色 Claudy pixel mascot。
- VS Code Codex：藍/青色、比較方正的螢幕或括號造型，可帶一個小 `<>` 或 split-panel motif。
- Windsurf Codex：青綠色波浪或帆形 companion，帶流動感的 pixel trail。
- Windsurf Cascade 原生 agent：沿用 Windsurf 色系，但加上階梯/瀑布狀條紋，和「Windsurf 裡的 Codex」分開。

不要只靠文字標籤區分。在 170x320 的螢幕上，形狀和顏色會比小字更容易辨識。

## 風險

- Codex hooks 目前不是完整安全 enforcement boundary；但 Claudy 只是狀態顯示，不是安全控制，風險可接受。
- Codex transcript 格式不是穩定介面，token 解析應維持 best-effort。
- Windsurf Cascade hooks 和 Codex hooks 是不同產品/不同 schema，即使都在 Windsurf 裡使用也不能混為一談。
- Windows PowerShell execution policy 可能擋腳本。
- USB serial driver、BOOT/RESET 模式在不同 Windows 機器上可能行為不同。
- mDNS 可能失敗，因此 IP 設定必須是一等公民。

## 建議實作階段

1. 將 bridge 拆成共用 transport 和 adapters，同時保持目前 Claude Code 行為不變。
2. 加入 Codex adapter 和 Windows/macOS/Linux 的 `.codex/hooks.json` 範例。
3. 加入 PowerShell wrappers 和 Windows setup/flash/test scripts。
4. 在 HTTP payload 和 firmware state 加入 `client`/`theme`。
5. 加入不同 client 的 mascot variants。
6. 加入 Windsurf Cascade adapter 和 installer。
7. 更新英文與繁中 README，補上 Windows 和 IDE-specific setup。

## 參考資料

- OpenAI Codex IDE extension: https://developers.openai.com/codex/ide
- OpenAI Codex Windows guide: https://developers.openai.com/codex/windows
- OpenAI Codex hooks: https://developers.openai.com/codex/hooks
- OpenAI Codex app for Windows: https://developers.openai.com/codex/app/windows
- Windsurf Cascade hooks: https://docs.windsurf.com/windsurf/cascade/hooks

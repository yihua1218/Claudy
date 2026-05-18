# Claudy

[English](README.md)

專為 **LilyGo T-Display-S3**（ESP32-S3，1.9 吋 170×320 ST7789）打造的 Claude Code 狀態吉祥物。

Mac 上的 Claude Code hook 事件會透過 HTTP 推送至 ESP32，裝置會繪製一個程序化產生的 Claude 吉祥物，即時反映 Claude 的工作狀態 — 閒置 / 思考中 / 工作中 / 等待中 / 錯誤 / 完成 — 並附帶 context token 進度條。

```
+----------------------------------+
|          工作中                    |
|  (face) [E] Edit                 |
|         firmware/firmware.ino    |
|         [██████░░░░] 85k / 200k  |
+----------------------------------+
```

## 架構

```
Claude Code (Mac)
   └─ hook 事件 JSON 透過 stdin 傳入
        └─ bridge/send-state.sh   （背景執行，立即返回）
             └─ bridge/send_state.py
                  └─ POST http://claudy.local/state
                       └─ ESP32 WebServer
                            └─ 螢幕繪製
```

Bridge 採用 fire-and-forget 模式，逾時 1.5 秒 — 絕不會阻塞 Claude。

## 硬體需求

- LilyGo T-Display-S3（ESP32-S3，8-bit parallel ST7789，16MB flash + PSRAM）
- USB-C 傳輸線

## 初次設定（Mac）

```bash
cd ~/Developer/Claudy

# 1. 安裝工具鏈
./scripts/setup.sh                              # 安裝 arduino-cli、ESP32 核心、函式庫

# 2. WiFi 認證資訊
cp firmware/config.h.example firmware/config.h
$EDITOR firmware/config.h                       # 設定 WIFI_SSID 與 WIFI_PASSWORD

# 3. 編譯與燒錄
./scripts/build.sh
./scripts/flash.sh                              # 自動偵測 /dev/cu.usbmodem*

# 4. 觀察首次開機
./scripts/monitor.sh
# 你應該會看到 "WiFi: connected, IP=..." 以及 "mDNS: http://claudy.local/"

# 5. 從 Mac 端驗證
curl http://claudy.local/state                   # GET — 取得裝置目前狀態（JSON）
curl http://claudy.local/screenshot.bmp -o claudy.bmp
./scripts/test-state.sh                          # 在螢幕上循環顯示所有狀態

# 6. 安裝 Claude Code hooks
./bridge/install-hooks.sh                        # 在既有 hooks 旁新增項目
# 重新啟動 Claude Code 以重新讀取 settings.json。
```

吉祥物現在應該會即時反映你在 Claude Code 中的所有操作。

## 狀態對應表

| Claude Code 事件                               | 吉祥物狀態  | 備註                                    |
|------------------------------------------------|-------------|-----------------------------------------|
| `SessionStart`                                 | 閒置        |                                         |
| `UserPromptSubmit`                             | 思考中      | 訊息 = 你輸入的提示詞前幾個字元         |
| `PreToolUse`                                   | 工作中      | 工具標記 + 簡短輸入內容（檔案/指令/模式） |
| `PostToolUse`                                  | 思考中      | 工具之間回到思考狀態                    |
| `PostToolUseFailure`                           | 錯誤        | 短暫閃爍後繼續下一個事件                |
| `Notification` / `Permission*` / `Elicitation` | 等待中      | 「核准 X？」提示                           |
| `Stop` / `TaskCompleted`                       | 完成 → 閒置 | Bridge 3 秒後自動淡出                   |
| `SessionEnd`                                   | 閒置        |                                         |

## 無裝置測試

瀏覽器 mock display、遠端主機設定、以及截圖方式請見[無裝置模擬選項](docs/no-device-simulation.zh-TW.md)。

```bash
echo '{"hook_event_name":"PreToolUse","tool_name":"Bash","tool_input":{"command":"ls"}}' \
  | python3 bridge/send_state.py
```

POST 會靜默失敗（網路上沒有裝置），但可以驗證對應路徑是否正常運作。

## 設定

### 韌體（`firmware/config.h`）

| 定義              | 預設值   | 說明                                                   |
|-------------------|----------|--------------------------------------------------------|
| `WIFI_SSID`       | –        | 你的 WiFi SSID                                         |
| `WIFI_PASSWORD`   | –        | 你的 WiFi 密碼                                         |
| `MDNS_HOSTNAME`   | `claudy` | mDNS 主機名稱（裝置可透過 `<hostname>.local` 存取）      |
| `HTTP_PORT`       | `80`     |                                                        |
| `AUTH_TOKEN`      | `""`     | 選填。若設定，請求必須帶上 `X-Claudy-Token: <value>`     |
| `BRIGHTNESS`      | `200`    | 0–255                                                  |
| `ENABLE_TOUCH`    | `0`      | 選用 CST816 觸控支援。請先確認你的板子觸控腳位後再啟用。 |
| `ENABLE_CODEX_UI` | `1`      | 顯示 Codex/client 視覺細節與觸控說明頁。                |
| `IDLE_TIMEOUT_MS` | `60000`  | 超過此時間沒有更新，自動回到閒置狀態                    |

### Bridge（環境變數）

| 變數                | 預設值                          |
|---------------------|---------------------------------|
| `CLAUDY_URL`        | `http://claudy.local/state`     |
| `CLAUDY_TOKEN`      | 空白                            |
| `CLAUDY_MAX_TOKENS` | `200000`（進度條的 context 預算） |

若要在 hooks 中使用這些變數，請在 `~/.zshenv` 中 export，這樣 Claude Code 啟動的每個 shell 都會繼承。

## HTTP API

### `POST /state`

```json
{
  "state":   "idle | thinking | working | waiting | error | done",
  "tool":    "Bash | Read | Edit | Write | Grep | Glob | WebFetch | Task | …",
  "message": "任意簡短文字",
  "tokens":  { "used": 12345, "max": 200000 }
}
```

除 `state` 外所有欄位皆為選填。回傳 `{"ok":true}`。

### `GET /state`

回傳目前狀態 + 運行時間 + IP。

### `GET /screenshot.bmp`

下載目前繪製畫面的 24-bit BMP。中立範例截圖請見 [Claudy 狀態截圖展示](docs/state-screenshot-gallery.zh-TW.md)。

### `GET /`

HTML 狀態頁面。

## 觸控互動

觸控是選用功能，因為不同 T-Display-S3 版本的觸控腳位可能不同。啟用 `ENABLE_TOUCH` 且偵測到 CST816 控制器後：

- 點左側寵物區：觸發短暫的本地「Boop」動畫。
- 點右側文字區：將目前畫面固定 30 秒。
- 左右滑動：切換狀態、client info、觸控說明頁。
- 上下滑動：調整亮度。
- 長按：鎖定或解除鎖定觸控輸入。

之後若要做螢幕上 approve，建議只在 Waiting/Approve 畫面啟用，而且採用長按再點擊之類的雙步確認，避免一般摸寵物時誤核准工具。

## 疑難排解

**燒錄時找不到序列埠**
請用 USB-C 連接。若仍找不到：按住 **BOOT**，點按 **RESET**，放開 **BOOT**，然後重試。用 `ls /dev/cu.*` 檢查 — T-Display-S3 會顯示為 `/dev/cu.usbmodem*`。

**編譯錯誤：找不到 PSRAM**
確認你燒錄的是 16MB/PSRAM 版本。`scripts/build.sh` 中的 FQBN 已設定 `PSRAM=opi,FlashSize=16M`。

**吉祥物停在開機畫面 / 顯示「WiFi failed」**
`config.h` 中的 SSID/密碼有誤，或你的 WiFi 僅支援 5GHz（ESP32-S3 僅支援 2.4GHz）。

**`claudy.local` 無法解析**
mDNS 有時在開機後需要 5–10 秒。改用裝置 IP 試試：`curl http://<claudy-ip>/state`。若你的網路 mDNS 不穩定，將 `CLAUDY_URL` 設為 `http://<claudy-ip>/state`。

**Hooks 沒有觸發**
執行 `install-hooks.sh` 後重新啟動 Claude Code。然後執行一個工具 — 你應該會看到吉祥物有反應。若要除錯，手動執行 `bridge/send_state.py` 並傳入範例事件。

## 移除

```bash
./bridge/uninstall-hooks.sh    # 僅移除 Claudy 項目，保留其他 hooks
```

## 目錄結構

```
firmware/         Arduino 草稿碼 — LovyanGFX 驅動、吉祥物、網頁伺服器
  firmware.ino    主程式
  display.{h,cpp} LGFX 面板設定 + 繪製管線
  mascot.{h,cpp}  程序化臉部繪製
  net.{h,cpp}     WiFi + mDNS + WebServer
  state.h         列舉 + 解析器
  config.h        WiFi 認證資訊（已加入 gitignore）
  sketch.yaml     Arduino CLI 設定檔

bridge/           Mac → ESP32 hook 橋接
  send-state.sh   bash 包裝腳本（由 hooks 呼叫；背景執行 python）
  send_state.py   事件 → 狀態對應 + POST
  install-hooks.sh
  uninstall-hooks.sh

scripts/          開發工作流程
  setup.sh        安裝 arduino-cli + ESP32 核心 + 函式庫
  build.sh        編譯韌體
  flash.sh        上傳至裝置
  monitor.sh      開啟序列埠監控
  test-state.sh   POST 所有狀態以驗證繪製結果
```

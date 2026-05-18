# 沒有板子時的模擬方式

這份文件說明 ESP32-S3 還沒到貨前，如何先測 Claudy。

## 方案 1：瀏覽器 Mock Display

最快的方式是使用內建 mock server：

```bash
./scripts/mock-server.py
```

然後用瀏覽器開啟：

```text
http://127.0.0.1:8765/
```

把 Claudy bridge 流量導到 mock API：

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
./scripts/test-state.sh
```

如果你的環境是 Windows VS Code + Remote-SSH + Codex，請在 Codex hooks 會執行的遠端 Linux 主機上啟動 mock server，然後用下面的環境變數啟動或重新啟動 Codex：

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
./bridge/install-codex-hooks.sh
```

mock server 實作：

- `POST /state`
- `GET /state`
- `GET /` 瀏覽器顯示畫面

這已足夠驗證 hook mapping、狀態切換、`client`、工具標籤和 token bar，不需要實體硬體。

### 取得螢幕截圖

新版 ESP32-S3 韌體提供一個 best-effort framebuffer screenshot endpoint：

```bash
curl http://<claudy-ip>/screenshot.bmp -o claudy.bmp
```

這會把目前的 offscreen render sprite 下載成 24-bit BMP。裝置在線時，這很適合快速除錯。

所有狀態的中立範例截圖可參考 [Claudy 狀態截圖展示](state-screenshot-gallery.zh-TW.md)。

若需要可重現的文件截圖，建議用相同 `/state` payload 在瀏覽器 mock display 重現畫面，再截瀏覽器頁面。mock 流程比較容易自動化，也不受相機角度、反光、WiFi 時序或實體 LCD 觀感影響。

啟動 mock server：

```bash
./scripts/mock-server.py
```

送出要截圖的狀態：

```bash
curl -X POST http://127.0.0.1:8765/state \
  -H 'Content-Type: application/json' \
  -d '{"state":"working","tool":"Bash","message":"screenshot test","client":"codex-vscode","tokens":{"used":42000,"max":200000}}'
```

接著開啟並截圖：

```text
http://127.0.0.1:8765/
```

截圖本身可用瀏覽器或作業系統的截圖工具完成。如果要捕捉實體 LCD 的真實觀感，就需要用相機拍攝；BMP endpoint 擷取的是 render sprite，不包含亮度、視角、反光等光學效果。

## 方案 2：從 Windows 瀏覽遠端 Linux 的 Mock Display

在遠端 Linux 主機上讓 mock server 綁定所有介面：

```bash
./scripts/mock-server.py --host 0.0.0.0 --port 8765
```

然後從 Windows 瀏覽器開啟：

```text
http://<remote-linux-ip>:8765/
```

Codex hooks 可以繼續 POST 到遠端主機本機：

```bash
export CLAUDY_URL=http://127.0.0.1:8765/state
```

也就是 hook 走本機 loopback，Windows 瀏覽器透過網路觀看畫面。

## 方案 3：Raspberry Pi HDMI 純 CLI 顯示

Raspberry Pi 可以實作相同 API，並在沒有 X-Window 的小 HDMI 螢幕上顯示 Claudy。建議架構：

- 跑一個相容 Claudy `/state` 的 HTTP server。
- 將最新 state 存在記憶體。
- 直接輸出到 Linux framebuffer，通常是 `/dev/fb0`。
- 依目前 framebuffer 解析度縮放版面。

可行的 rendering backend：

- Python + Pillow 先畫成 RGB image，再轉成 framebuffer pixel 寫入 `/dev/fb0`。
- Python + `pygame` 使用 framebuffer console backend，如果環境支援。
- 用 C 或 Rust 寫小型 service，透過 framebuffer ioctls 取得更好的效能。

建議維持相同 API：

```json
{
  "state": "idle | thinking | working | waiting | error | done",
  "tool": "Bash | Read | Edit | Write | Grep | Web | Task | Tool",
  "message": "short text",
  "client": "codex-vscode-remote",
  "tokens": { "used": 42000, "max": 200000 }
}
```

Pi 版本也應提供 `GET /state`，讓 bridge、瀏覽器 mock、ESP32 韌體、Pi renderer 都共用同一個心智模型。

## 不同螢幕尺寸

版面應該用解析度無關的方式計算：

- 將畫面分成 mascot 區和資訊區。
- padding、字體大小、token bar 尺寸都根據 framebuffer 寬高計算。
- 不同尺寸仍保留相同 state colors 和 client visual identity。
- 很小的螢幕優先顯示 state、mascot/client identity、tool、一行 message。
- 較大的 HDMI 螢幕可以再顯示 token 細節、event name、model、last-update timestamp。

這樣同一個 `/state` API 可以驅動：

- ESP32-S3 320x170 landscape 螢幕。
- 瀏覽器 mock display。
- Raspberry Pi HDMI framebuffer。
- 未來的小型 SPI 或 e-paper display。

## 建議開發流程

1. 先在 Remote-SSH Linux 環境啟動 `scripts/mock-server.py`。
2. 用 `bridge/install-codex-hooks.sh` 安裝 Codex hooks。
3. 在瀏覽器確認 Codex 操作會即時更新畫面。
4. 如果 ESP32-S3 到貨前需要一個實體常駐顯示器，再基於同一個 `/state` contract 實作 Raspberry Pi renderer。
5. ESP32-S3 到貨後，把 `CLAUDY_URL` 從 mock 或 Pi endpoint 換成 `http://<claudy-ip>/state`。

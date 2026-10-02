# GUI 端到端實測：Simulator 與正式程式（2026-10-02）

- 環境：同 [2026-10-02-virtual-com-lifecycle.md](2026-10-02-virtual-com-lifecycle.md)；COM10↔COM11、COM12↔COM13 兩組配對，com0com 2.2.2.0，鮑率模擬開啟。
- 程式版本：`d99c349`，`build/` 的 Debug 建置。
- 操作方式：GTK4 在 Windows 上不向 UI Automation 公開控制項，設了 `GTK_A11Y=accesskit` 也一樣；以 PostMessage 送出的點擊也不會被處理。因此改用真實滑鼠輸入（SendInput）操作視窗，再以 PrintWindow 截圖判讀結果。

## 步驟與結果

| # | 操作 | 結果 |
|---|---|---|
| 1 | 開啟 Simulator | 自動檢查環境，不需 UAC；大字顯示「輸入 COM 選 COM11、輸出 COM 選 COM12（38400 bps）」；TX／RX 自動填入 COM10／COM13 |
| 2 | Simulator「配對雙向自測」 | 四個方向都通過，14.9–16.2 ms |
| 3 | 正式程式選 COM11／COM12，按「連線 COM」 | Connected；連線設定被鎖定 |
| 4 | Simulator「開始測試」（固定 0°，25 Hz） | 正式程式 RX 154、TX 153、Invalid 0；Simulator RX valid 153、invalid 0、garbage 0，Session 24.75 Hz |
| 5 | 正式程式勾選 Sweep ON（±10°，4 s） | Offset 先負後正；Simulator 收到 `:000000  0000G-0792  0000`，即 Roll −7.92°，Pitch／Heave 原樣保留；RX 2284 筆全部有效 |
| 6 | Simulator「Stop TX」 | 正式程式顯示「Input stale — output suspended」，Sweep 自動取消；輸出停在 2787 筆，與 Simulator RX 的 2787 筆相同 |
| 7 | Simulator「Start TX」 | 正式程式顯示「Input recovered — Sweep OFF」，輸出恢復；Simulator 收到的 Roll 為 0.00 |
| 8 | 固定 0° frame OFF 比對 | RX 453 筆、invalid 0，**OFF PASS** |
| 9 | 正式程式 中斷 → 連線 → 中斷 | 正常；重新連線時計數歸零，中斷後設定解鎖 |
| 10 | 開著檔案選擇視窗時關閉 Simulator | 正常結束 |
| 11 | 關閉正式程式 | 正常結束；COM10–13 仍可使用 |

## 發現的問題（尚未修正）

1. **輸出時間抖動**：25 Hz 的理論間隔是 40 ms，實際為 10.2–68.2 ms；約六成的輸出遲到超過 1 ms，最大遲到 15.8 ms，符合 Windows 預設計時器的粒度。100 Hz 時會更嚴重。
2. **速率顯示**：正式程式的 Input／Output Hz 是從按下連線起算的累積平均，包含還沒收到資料的時間，所以顯示 19 Hz，實際卻是 25 Hz；畫面上也沒有即時速率。
3. **間隔統計**：資料過期期間的空檔也被算進間隔統計，最大值因此變成 22478 ms。
4. **過期時的數值**：資料過期時，畫面仍顯示最後一筆的 Real／Offset／Output，沒有標示為舊值。
5. **介面語言**：兩個程式都中英混雜（例如 Output rate、Start TX），acceptance 要求繁體中文。
6. **Simulator 版面**：狀態文字長度一變，整頁的捲動位置就會跳動。

## 未涵蓋

- 50／100 Hz
- Replay 檔與嚴格模式
- 套用 Sweep 設定、反轉方向
- 從按鈕執行需要 UAC 的環境動作，以及取消 UAC 的情況

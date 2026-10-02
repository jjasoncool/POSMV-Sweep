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

## 發現的問題（已於同日修正，見下方複測）

1. **輸出時間抖動**：25 Hz 的理論間隔是 40 ms，實際為 10.2–68.2 ms；約六成的輸出遲到超過 1 ms，最大遲到 15.8 ms，符合 Windows 預設計時器的粒度。100 Hz 時會更嚴重。
2. **速率顯示**：正式程式的 Input／Output Hz 是從按下連線起算的累積平均，包含還沒收到資料的時間，所以顯示 19 Hz，實際卻是 25 Hz；畫面上也沒有即時速率。
3. **間隔統計**：資料過期期間的空檔也被算進間隔統計，最大值因此變成 22478 ms。
4. **過期時的數值**：資料過期時，畫面仍顯示最後一筆的 Real／Offset／Output，沒有標示為舊值。
5. **介面語言**：兩個程式都中英混雜（例如 Output rate、Start TX），acceptance 要求繁體中文。
6. **Simulator 版面**：狀態文字長度一變，整頁的捲動位置就會跳動。

## 修正後複測（同日）

修正內容：
- 以高解析度可等待計時器排程。
- 新增 `TimingStats`，提供即時與平均速率、中斷次數與 p99。
- 引擎改為回報五種明確狀態。
- 兩個程式的介面改成中文。
- 資料過期時標示最後值，趨勢圖在過期與未連線期間斷開。
- Simulator 的狀態區改為固定高度、送出與接收左右並排。

排程器（Simulator 命令列，輸出到檔案）：

| 速率 | 筆數 | 修正前：遲到 >1 ms／最大遲到 | 修正後 |
|---|---|---|---|
| 25 Hz | 100 | 94／20.4 ms | 0／0.70 ms |
| 50 Hz | 200 | 178／20.0 ms | 0／0.59 ms |
| 100 Hz | 400 | 371／29.6 ms | 0／0.76 ms |

GUI 端到端（25 Hz、固定 0°，COM10→11→12→13）：

| 檢查 | 結果 |
|---|---|
| 速率顯示（第 2 項） | 正式程式輸入與輸出都是即時 25.0 Hz、平均 25.1 Hz；Simulator 接收為即時 26.0、平均 25.1 Hz |
| 輸出時間（第 1 項） | 正式程式輸出間隔平均 39.9 ms、最大 40.4 ms、p99 40.5 ms；遲到超過 1 ms 0 筆；Simulator 接收間隔 p99 40.5 ms |
| 資料過期（第 4 項） | 狀態顯示「資料過期：已暫停輸出並關閉 Sweep」；數值標為「最後值（已停止輸出）」；即時速率 0.0 Hz；趨勢線在過期期間斷開 |
| 恢復（第 3 項） | 狀態顯示「運作中（輸入曾過期，Sweep 已自動關閉）」；中斷 1 次；輸出間隔最大 40.5 ms，沒有被空檔拉高 |
| 介面語言（第 5 項） | 兩個程式的標籤、按鈕與狀態都是中文，保留 Roll、Sweep、COM、CENTER 等術語 |
| 版面跳動（第 6 項） | 停止、勾選、開始測試的過程中，按鈕位置不變 |
| OFF 比對 | 有效 124、無效 0，通過 |
| 關閉程式 | 兩個程式都正常結束 |

## 未涵蓋

- 50／100 Hz
- Replay 檔與嚴格模式
- 套用 Sweep 設定、反轉方向
- 從按鈕執行需要 UAC 的環境動作，以及取消 UAC 的情況

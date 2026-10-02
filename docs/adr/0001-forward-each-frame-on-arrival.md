# 收到就轉送，不再以固定頻率重新取樣

正式程式原本以 25／50／100 Hz 的固定頻率重送最新的有效 Frame。TSS1 沒有時間欄位，MBES 以收到資料的時刻作為時間戳，所以重新取樣會額外造成 0 到一個輸出週期的不固定延遲（25 Hz 時最多 40 ms），讓 MBES 抵銷真實 Roll 時產生誤差，例如 15.7°/s × 40 ms ≈ 0.6°。依據見 [2026-10-02-mbes-motion-discontinuities.md](../research/2026-10-02-mbes-motion-discontinuities.md)。

2026-10-02 決定改為每收到一筆有效 Frame 就立即處理並送出：Sweep OFF 時原樣轉送，ON 時只改寫 Roll。輸出頻率因此等於 POS MV 的輸出頻率；需要不同頻率時，改 POS MV 的設定（支援 1–200 Hz）。實測平均轉送延遲為 0.07 ms（[測試台報告](../verification/2026-10-02-sweep-bench.md)）。

## Considered Options

- 保留重新取樣，但預設改為 100 Hz：延遲變動可縮到 10 ms 以內，但仍然不固定。

## Consequences

- 「輸出速率」設定已移除；驗收中的 25／50／100 Hz 改指輸入頻率。
- 資料過期時，因為沒有新的輸入，自然也就沒有輸出。

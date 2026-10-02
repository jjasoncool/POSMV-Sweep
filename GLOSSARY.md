# POSMV-Sweep

在 POS MV 與下游 MBES 之間轉送 TSS1 姿態資料的串列 Proxy，可在 Roll 上疊加週期性的 Offset，讓 MBES 的波束左右擺動，從不同角度掃描海底管線以建立模型；另有 Simulator 作為獨立的測試控制台。最終成果以 POS MV 自己記錄的真實姿態處理。

## Language

### 系統與程式

**POS MV**:
提供姿態資料的定位定姿系統，是 Proxy 在現場的資料來源。

**MBES**:
多波束測深儀，是 Proxy 在現場的輸出對象。

**Proxy**（正式程式）:
在 POS MV 與 MBES 之間轉送 TSS1 的正式程式；只改寫 Roll，其餘欄位原樣保留。
_Avoid_: Sweep（指程式時）、主程式

**Simulator**:
獨立的測試控制台，負責建立 Virtual COM Environment、送出測試資料給 Proxy，並接收、驗證 Proxy 的輸出。

**Validator**:
Simulator 中檢查接收資料並產生 Verdict 的部分；不是獨立的程式。

### 資料

**TSS1**:
POS MV 輸出的 ASCII 姿態資料格式，含 Heave、Roll、Pitch 與狀態旗標。

**Frame**:
一筆 TSS1 資料：25 bytes 本體加上 CRLF。
_Avoid_: record、packet、sentence、motion

**Valid Frame**（有效 Frame）:
通過 TSS1 格式檢查的 Frame。Proxy 每收到一筆就轉送一筆；無效的丟棄並計數，不轉送。
_Avoid_: latest motion

**Capture**（capture 檔）:
錄製下來的 TSS1 文字檔，作為 Replay 的來源。
_Avoid_: Replay 檔案、fixture

**Replay**:
Simulator 依選定速率送出 Capture 內容的動作。

**Preflight**（預檢）:
Replay 前對整份 Capture 的檢查，統計有效 Frame、異常與垃圾資料；分為 Tolerant（容錯，預設）與 Strict（嚴格）兩種模式。

### Sweep

**Real Roll**:
輸入 Frame 中的 Roll 值。

**Sweep**:
在 Real Roll 上疊加週期性 Offset，讓下游 MBES 波束左右擺動的功能；狀態只有 ON 與 OFF。
_Avoid_: 以 Sweep 稱呼 Proxy 或它的埠（例如 Sweep Input）

**Offset**:
Sweep 疊加在 Real Roll 上的角度。
_Avoid_: Synthetic Roll、合成 Roll

**Output Roll**:
Real Roll 加上 Offset、限制在 ±90° 內之後，寫入輸出 Frame 的 Roll。

**Angle**（幅度 ±deg）:
Offset 的最大絕對值。

**Cycle**（完整週期 sec）:
Offset 走完一輪 CENTER → 一側峰值 → CENTER → 另一側峰值 → CENTER 所需的時間。

**CENTER**:
Offset 為零的相位起點；套用設定與 OFF→ON 時，Sweep 都從這裡重新開始。

**Direction**（方向）:
Offset 離開 CENTER 後先往哪一側擺；預設 Negative-first（先負），可反轉為 Positive-first。

**Fixed Offset**（固定 Offset）:
以固定角度取代週期性 Offset 的驗證模式，用於現場確認 Roll 的正負號與擺動方向。

**Finish at CENTER**（收尾到 CENTER）:
關閉 Sweep 或套用新設定時，讓 Offset 沿正弦走到下一個 CENTER 才停止或切換，不立即跳回；由操作員選擇，預設不啟用。

**Stabilisation Limit**（MBES 穩定上限）:
下游 MBES 能補償的最大 |Roll|，依機型由操作員設定。Output Roll 超過時，MBES 無法維持波束指向。

**Peak Sweep Rate**（最大擺動速度）:
Offset 變化最快時的角速度，等於 2π × Angle ÷ Cycle；部分 MBES 會標記 Roll 變化過快的 ping。

### 埠與環境

**Port**（COM）:
Windows 上的一個 COM 埠，可以是實體或虛擬的。

**Virtual Pair**（配對）:
由 com0com 建立、彼此相通的兩個 Endpoint。

**Endpoint**（端點）:
Virtual Pair 的其中一端；它本身就是一個 Port。

**Input / Output**（輸入 COM／輸出 COM）:
Proxy 開啟的兩個 Port：Input 接收 POS MV（測試時為 Simulator TX）的資料，Output 送往 MBES（測試時為 Simulator RX）。
_Avoid_: Sweep Input、Sweep Output

**TX / RX**:
Simulator 開啟的兩個 Port：TX 送資料到 Proxy 的 Input，RX 接收 Proxy 的 Output。

**Virtual COM Environment**（測試環境）:
Simulator 建立的兩組 Owned Pair，以及每個 Endpoint 對應的角色（TX、Input、Output、RX）。

**Owned Pair**（本程式配對）:
由本程式建立並記錄擁有權的 Virtual Pair；本程式只會移除這種配對。

**Foreign Pair**（其他配對）:
不是本程式建立的 Virtual Pair；本程式不會接管或移除。

### 狀態與判定

**Stale**（資料過期）:
Input 超過 Stale Timeout 沒收到有效 Frame 的狀態。

**Stale Timeout**:
判定 Stale 的時間門檻。

**Verdict**（判定）:
Validator 對接收資料的結論，分為 Protocol、Timing、Behavior 三種，彼此獨立。

**Indeterminate**（無法判定）:
證據不足以下結論時的 Verdict，例如 Replay 時無法對齊輸入與輸出。

**Observed Interval**（接收間隔）:
RX 相鄰兩次收到 Frame 的時間差；不等於來源端的發送抖動。

**Current Rate**（即時速率）:
最近 1 秒內的 Frame 數。

**Average Rate**（平均速率）:
本次連線中，從第一筆 Frame 起、資料流動期間的平均速率；不含 Gap。
_Avoid_: Session Average、累積平均

**Gap**（中斷）:
兩筆 Frame 之間超過 Stale Timeout 的空檔；不計入間隔統計。

**Forwarding Delay**（轉送延遲）:
Proxy 從收到一筆 Frame 到寫出的時間。

### 測試

**Fixed Frame OFF Test**（固定 frame OFF 比對）:
Sweep OFF 時，以已知的固定 Frame 逐 byte 比對 Proxy 輸出的測試。

**Fault Injection**（故障注入）:
Simulator 依設定刻意送出的異常資料：Malformed、Split、Garbage、Drop。

**Frame Matching**（比對）:
RX 依 Roll 以外的內容，把收到的 Frame 對回 TX 送出的那一筆。Proxy 逐筆轉送，所以 Capture 與 Simulator 產生的資料都能比對。
_Avoid_: 配對（指 Virtual Pair）

**Tag**（序號）:
Simulator 寫在自己產生的測試 Frame 的 Heave 欄位裡的流水號，讓每筆 Frame 內容都不同，比對時能準確找出遺漏。Capture 不加。

**Delay**（延遲）:
同一筆 Frame 從 TX 送出到 RX 收到的時間，包含兩組 Virtual Pair 與 Forwarding Delay。

**Measured Offset**（量測到的 Offset）:
RX 收到的 Roll 減去比對到的送出 Frame 的 Roll，也就是 Proxy 實際加上的 Offset，與船體 Roll 無關。

**Missing**（遺漏）:
送出後沒有回到 RX 的 Frame 數；5 秒內沒回來就算遺漏。

**Unmatched**（對不上）:
RX 收到、但對不回任何送出 Frame 的筆數。

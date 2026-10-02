# MBES 遇到姿態資料中斷與跳變時的反應：原廠文件調查（2026-10-02）

## 問題

Proxy 在 Sweep 進行中，若 Input Stale（≥ 500 ms）就會停止輸出；恢復時 Sweep 為 OFF，MBES 收到的 roll 會從最後一個 swept 值直接跳到 Real Roll。操作員中途關掉 Sweep，或在 ON 時 Apply 新的 Angle／Cycle，Offset 也會瞬間回到 0；只有 OFF → ON 是從 CENTER 開始，角度連續。使用者（水文工程師）想知道：MBES 的波束角度會不會因此「沒有重新校準／還沒穩定」而跟不上？Proxy 是否應在恢復後保留一段 settling 時間，再讓 Offset 從 0 開始？本文只整理原廠手冊、資料格式文件、規格書與軟體說明查得到的內容。對 POSMV-Sweep 的做法只列出選項，不替使用者做決定。

引用寫成〔代號 頁碼〕，代號見文末「來源」。標「推論」或「計算」的內容不是原文。

## 結論摘要

- **本次查到的原廠文件都沒有要求 MBES 做「settling／重新校準」。** 查到的 MBES 都用電子式 beamforming 做 roll stabilisation，每個 ping 直接使用即時 roll。Kongsberg EM 2040 寫明「The beamforming uses real-time roll」〔K1 p.9〕，EM 712 寫明「applied in real time」〔K2〕；RESON 7k 逐 sample 記錄所用的 roll〔R1 p.62–63〕；R2Sonic 寫明「stabilises the sonar for the motion」〔S1〕。沒有任何一份文件要求 motion 恢復後讓 sonar warm-up。信心：中（文件只描述即時套用，沒寫不代表沒有，仍需實測）。
- **TSS1 的 "settling" 是 motion sensor 自己的狀態，與 sonar 無關。** 小寫旗標表示來源還在 settling〔A1 p.3-20〕；TSS 系統的定義是開機、改模式或改 heave bandwidth 後的 3 分鐘〔T1〕；POS MV 的 settling 指 heave filter 的暫態〔A1 p.4-21, p.B-15〕。Proxy 原樣轉送旗標。推論：Proxy 停送不屬於上述任何條件，POS MV 不會因此重新 settle。信心：旗標定義為高；POS MV 內部如何決定旗標為中（V4 文件沒有說明）。
- **文件寫明的限制是 stabilisation 的角度範圍；反應速度沒有文件化的上限。** NORBIT 只補償 ±10° roll，swath 上限 160°〔N1 p.48〕；R2Sonic 只能在最大 sector 內調整〔S1〕；作為對照，EM 2040 與 EM 712 的 roll 範圍是 ±15°〔K1 p.51〕〔K2〕（Kongsberg 不收 TSS1）。預設 Angle ±10° 加上 Real Roll 可能超出範圍。信心：規格數字為高；超出範圍後實際怎麼處理為低（文件沒寫）。
- **Motion 中斷期間和恢復當下 sonar 怎麼做，公開文件沒有交代。** 只查到告警與旗標：SIS 依「Expected receiver frequency」發出告警〔K4 p.175〕，RESON 7012 有「External motion data not received」旗標〔R1 p.63〕，HYPACK 的裝置逾時預設 15 s〔H1 p.2-209〕。各廠都沒說明中斷時是保留最後的值、歸零，還是停用 stabilisation，所以恢復瞬間的跳變大小要看機型。信心：中。
- **跳變會不會造成資料問題，取決於 swept roll 有沒有被拿去做 georeferencing。** R2Sonic「will always report the real launch angle」〔S1〕；NORBIT 的 roll stabilisation「unrelated to roll compensation for georeferencing」〔N1 p.48〕。只要真實姿態另外送進擷取軟體，跳變就只是某些 ping 的波束指向突然改變。如果擷取軟體也讀這條 swept stream，跳變就成了 roll spike，後處理會濾掉或拒收〔C1〕。信心：中。
- **原廠支援讓 swath 偏向一側的方式是原生 steering。** R2Sonic 可以旋轉 swath sector〔S2〕，NORBIT 有 Direction〔N1 p.43〕，RESON 有 FlexMode sector steering〔R1 p.137〕，Kongsberg 可以分別設定 Port／Starboard angle〔K4 p.118〕。本次沒有找到任何原廠描述「注入合成 roll」，也沒有找到「姿態突變時要 ramp 或 warm-up」的原廠指引。信心：中（沒找到不代表不存在）。

## 1. MBES 如何即時套用姿態

### Kongsberg EM（EM 2040、EM 712；EM 304、EM 124 的規格書本次未取得）

- EM 2040 寫明「The beamforming uses real-time roll (i.e. the beams are fully roll stabilized)」〔K1 p.9〕，並寫明「The receive beams are electronically stabilized for roll」。TX 與 RX 都可做 pitch 穩定，規格為 roll ±15°、pitch ±10°、yaw ±10°〔K1 p.51〕。EM 712 的範圍相同，並寫明「Roll, pitch and yaw stabilization is applied in real time」〔K2〕。
- 計算深度時，EM 2040 同時使用 transmit 與 receive 兩個時刻的姿態，並把已套用的 stabilisation 一起算進去〔K1 p.10, p.51〕。
- 每個 ping 會依前一個 ping 調整：TX 依前一個 ping 的距離做 focusing；每個 ping 的最大量程自動決定，並預留下一個 ping 可能變深的空間〔K1 p.9〕。推論：swath 突然擺動時，這些以前一個 ping 為基準的自動設定可能要一兩個 ping 才跟得上，但文件沒有描述這種情況。
- 姿態輸入是 10 bytes 的 EM attitude 格式，更新率「typically up to 100 Hz」。同時接多個 sensor 時，即時只會用其中一組〔K3 p.21, p.23〕。EM 2040P 的序列埠只接受 EM Attitude 3000；其他格式走 Ethernet，例如 Seapath Binary、POS-MV GRP 102/103、KM Binary〔K5 pp.101–102〕。清單裡沒有 TSS1，所以 Kongsberg EM 不會是 TSS1 的下游。
- 文件沒有說明內部濾波或轉向速率上限。Kongsberg 只要求採用 sensor 原廠提供的延遲值，並寫明「a possible prediction facility in the sensor should not be used」〔K4 p.26〕。

### Teledyne RESON SeaBat（7k Data Format v2.41；T-series 是否完全相同未確認）

- Motion sensor 可設定為 TSS1、SIMRAD EM1000／EM3000、$PASHR 或 OCTANS TAH，並有「Motion latency」可設 0–0.050 s〔R1 p.136〕。
- 開啟 roll stabilisation 後，beam pattern 以垂直方向為基準；pitch 由 projector steering 處理〔R1 p.43, p.53〕。
- 7012 Ping Motion Data 記錄 detection 計算所用的參數：pitch 只記錄 ping 當下的一個值，roll、heading、heave 則逐 sample 記錄〔R1 p.62–63〕。推論：一個 ping 的接收期間，roll 是逐 sample 更新的。
- 7012 的 error flags 包含：roll > 15°、roll > 35°、roll rate > 10°/s，以及「External motion data not received (roll angle and rate are not reported)」〔R1 p.63〕。文件沒有說明旗標觸發後 sonar 會不會改變行為。
- Depth gate 也用到 roll：「Roll value (in radians) applied to gates; zero if roll stabilization is ON」〔R1 p.65〕。
- T-series 的 roll stabilisation 角度上限：本次未查到。

### R2Sonic（Sonic 2020／2024／2026）

- 可從 serial 或 Ethernet 接收 TSS1、iXSea $PHOCT、CDL MiniPOS3〔S1〕。
- FAQ 原文：「R2Sonic does not compensate the sonar data for motion. R2Sonic MBES stabilises the sonar for the motion.」「The sonar will always report the real launch angle」。Roll 與 pitch stabilisation「can be turned on and off at any time」，且「Roll stabilisation can only adjust the sonar within the maximum sector」〔S1〕。
- 手冊（只取得搜尋索引摘錄）寫道：swath rotation 或 sector 超出 160° 上限時，系統會停止 roll stabilisation；切換 stabilised 與 non-stabilised 不需要停止記錄〔S3〕。
- 計算：最大 sector 為 160°。若 sector 設為 W，留給 rotation 與 stabilisation 的空間約 ±(160 − W)/2；例如 W = 130° 時約為 ±15°。

### NORBIT（WBMS／WINGHEAD，standalone）

- Standalone 機型是把外部 motion sensor 的 TSS1 接到 GUI 電腦的 COM 埠，用來做 roll stabilisation〔N1 p.40〕。
- 原文：「Roll stabilization compensates up to ±10° of roll and is limited to 160° swath angle. Note that this is unrelated to roll compensation for georeferencing, which is handled separately.」〔N1 p.48〕
- GUI 上有 Roll Stab 狀態燈〔N1 p.41〕。

### 小結

四家都是電子式 steering（推論：沒有機械慣性需要等待）。文件寫到的限制只有角度範圍（±10°～±15°、160° sector），都沒有寫轉向速率上限；唯一跟速率有關的是 RESON 在 roll rate > 10°/s 時加上旗標。

## 2. Motion 資料中斷與恢復

- Kongsberg SIS 5 的 Sensor options 可以設定「alarm settings for loss of attitude data」，判斷依據是「Attitude, Expected receiver frequency (Hz) (0=disable)」〔K4 p.175〕。
- RESON：7012 的 error flag bit 7「External motion data not received」〔R1 p.63〕。
- NORBIT：有 Roll Stab 狀態燈〔N1 p.41〕；疑難排解只建議用終端程式確認 TSS1 資料流〔N1 p.55〕。
- HYPACK：任一裝置最後一筆資料的時間超過 Timeout Interval 時，Devices 告警會轉紅，「It defaults to 15 seconds」；其他個別裝置的告警則寫死為 30 s〔H1 p.2-209〕。HYSWEEP 的 MRU 告警在一段時間沒收到資料後轉紅〔H1 p.3-157〕。
- R2Sonic：FAQ 沒有提到 motion 中斷時的行為〔S1〕。
- 沒有找到明文說明中斷時 sonar 會保留最後的值、歸零、外插或停用 stabilisation；也沒有找到恢復後必須重新初始化、settling 或 warm-up 的明文。
- 文件中提到「等待穩定」的都是 motion sensor：POS MV 的 heave filter 在開機、或垂直位移突然變化後需要時間 settle〔A1 p.4-21, p.B-15〕；TrueHeave 開始導航後最多需要 5 分鐘初始化〔A1 p.6-2〕；Kongsberg 做 roll 校正測線時要求「sufficient lead-in time to allow the roll sensor to stabilize」〔K4 p.44〕。

## 3. 擷取與後處理軟體對跳變、spike、gap 的處理

- Kongsberg：姿態以連續時間序列記錄，供後處理修正〔K3 p.39〕；EM 格式的 sensor status 會複製到記錄的 attitude datagram〔K3 p.67〕。
- CARIS HIPS and SIPS：Filter HIPS Attitude 提供 Moving Average 與 Fast Fourier 兩種濾波，門檻以標準差的倍數設定（預設 1）。資料被拒收後留下的空檔，預設以內插補上；勾選「Reject with Break Interpolation」時，則「no interpolation of the gaps will take place and soundings associated with the rejected sensor data will also be rejected」〔C1〕。Import Auxiliary Data 的 Maximum Allowed Gap 預設為 2 s〔C2〕。
- QPS：Qinsy 的 QMS 有「Roll outside limit」告警〔Q1〕；Qimera 遇到檔案邊界時，會從 1 s 內的相鄰檔取時間序列來內插〔Q2〕。沒有找到 Qinsy 或 Qimera 對姿態 gap 的門檻說明。
- RESON：roll 角度、roll rate 與 motion 缺失會記錄在 7012 旗標中〔R1 p.63〕。
- 推論：swept roll 若只送進 sonar 的 stabilisation（R2Sonic 與 NORBIT 的設計），擷取軟體看到的是正確的 launch angle，加上另一路的真實姿態，跳變不會變成姿態 spike。若擷取軟體也讀同一條 swept stream，跳變與 Sweep 本身都會直接進入 georeferencing，並可能被上述濾波當成 spike。

## 4. TSS1 狀態旗標

- POS MV：U／u 為 unaided、G／g 為 GPS aided、H／h 為 heading aided、F／f 為 full aided；大寫是「settled condition」，小寫是「settling」〔A1 p.3-20〕。輸出率 1–200 Hz，25 Hz 時建議至少 9600 baud〔A1 p.3-21〕。
- TSS 系統的定義（UHDAS 文件中 MAHRS 診斷頁所列，屬轉載）：小寫表示「still awaiting the end of the 3 minutes settling period after power-on or a change of mode or heave bandwidth」。文件也註明 gyrocompass 開機後要幾小時才穩定，而「The status flag does NOT indicate this condition」〔T1〕。
- SBG 的小寫代表「unstable」，例如 u 是「Unaided mode but unstable heave data」；SBG 的 Simrad 3000 狀態 0x9A 則表示「device is aligning, or the heave filter is unsettled」〔T2〕。NovAtel 只用兩個值：F（INS Active）與 H（INS 尚未完成 alignment）〔T3〕。可見各廠旗標的意義並不一致。
- 對照 Kongsberg EM 格式的狀態值：90h 表示完整精度，91h–99h 精度降低，9Ah–9Fh 無效但正常運作，A0h–AFh 為 sensor 錯誤〔K3 p.23〕。文件只說狀態會被記錄，沒說即時處理時會不會拒用〔K3 p.67〕。
- POS MV 如何把內部狀態對應到 TSS1 旗標：V4 Operation Guide 與 V4 User ICD 都只列出代碼，沒有說明〔A1 p.3-20〕〔A2 pp.97–98〕。
- 沒有找到依旗標拒收或降低權重的 sonar 或軟體。RESON 7k 的 1012／1016 姿態 record 沒有狀態欄位〔R1 p.34, p.36〕，所以 TSS1 旗標至少不會記錄在 s7k 裡。

## 5. 時間標記（time-tagging）

- TSS1 沒有時間欄位〔A1 p.3-20〕。在 POS MV 的二進位輸出格式中，TSM 5265 帶有 time tag，TSS (Format 1) 則沒有〔A2 pp.97–98〕。
- Kongsberg：「a time stamp is generated at the reception of the first byte of the datagram」，並可設定 attitude delay〔K4 p.26, p.139〕。姿態同步的要求是「better than 5 ms for attitude, with roll being the most critical parameter」〔K4 p.24〕，並要求「The delay in the data from the motion sensor must be constant and known to within 5 ms」〔K4 p.27〕。
- RESON：系統時間以 PPS 加 ZDA 同步。位置使用訊息內的時間，其他 sensor 則「use time of arrival of the first character」〔R1 p.17〕。Motion latency 可設 0–50 ms〔R1 p.136〕。
- R2Sonic：要精確打時間戳，必須接 PPS 與 ZDA（或 UTC）〔S1〕；由 SIM 負責「time tags sensor data」〔S2〕。
- NORBIT：standalone 機型需要 PPS 與 NMEA ZDA〔N1 p.24〕；TSS1 經 GUI 電腦的 COM 埠進入〔N1 p.40〕。推論：時間戳可能是在 PC 端打上的，需要確認。
- HYPACK 對 latency 的定義是：sensor 完成量測到輸出給電腦之間的時間，用來正確標記時間〔H1 p.2-219〕。
- 計算：Proxy 以最新有效 Frame 重新取樣，額外延遲在 0 到一個輸出週期之間變動（25／50／100 Hz 分別為 0–40／0–20／0–10 ms），還要再加上輸出線路的傳送時間（27 bytes × 10 bit，9600 baud 約 28 ms，38400 baud 約 7 ms）。指向誤差約等於 roll rate × 時間誤差，例如 15.7°/s × 40 ms ≈ 0.6°。這種不固定的延遲不符合 Kongsberg「constant and known to within 5 ms」的要求，但 Kongsberg 本來就不收 TSS1。對 R2Sonic 與 NORBIT 的 stabilisation 路徑來說，如果真實姿態另外送進擷取軟體，影響的是波束指向，而不是 georeferencing。

## 6. 原生 swath steering、roll offset 與 ramp 指引

- 原生 steering：
  - R2Sonic 提供「the ability to rotate the swath sector」，可在測量中「'on-the-fly', in real-time」調整〔S2〕。
  - NORBIT 的 Direction 寫明「Direction of swath pointing angle … Useful when surveying slopes, shoreline or for mapping vertical structures」；Equidistant 模式下，「a 160° swath with 0° rotation or a 120° swath pointed 20° is the maximum」〔N1 p.43〕。
  - RESON FlexMode 的參數寫明「Sector steering angle in radians (positive to starboard)」〔R1 p.137〕；T50-S 的 Pipe Detection & Tracking 可自動 steering FlexMode sector〔R2〕。
  - Kongsberg 可在作業中分別設定 Port 與 Starboard angle〔K4 p.118〕。
- 靜態 roll offset：Kongsberg 的安裝參數有 motion sensor roll offset（MSR）〔K3 p.83〕，SIS 也有 angular offset〔K4 p.139〕；兩者都是安裝校正用，不是用來 steering。
- 本次沒有找到原廠描述用合成 roll 讓 swath 擺動的做法，也沒有找到「姿態突變時要 ramp、smoothing 或 warm-up」的原廠指引。Kongsberg 反而要求延遲固定，並且不要使用 sensor 的 prediction〔K4 p.26–27〕。

## 對 POSMV-Sweep 的影響與可行做法

### 目前四種情況在 MBES 端的表現（依上述事實推論與計算）

1. Input Stale ≥ 500 ms：Proxy 停止輸出，MBES 這段時間會怎麼做不清楚（可能保留最後的值、歸零或停用 stabilisation）。恢復時 Sweep 為 OFF：如果 MBES 保留了最後的值，跳變大小等於最後的 Offset（≤ Angle）；如果 MBES 停用了 stabilisation，則是從未穩定直接切回依 Real Roll 穩定。若 HYPACK 也讀這條資料，0.5–15 s 的中斷可能不會觸發它的預設告警〔H1 p.2-209〕。
2. Sweep 中途 OFF：Offset 瞬間歸零，跳變 ≤ Angle。
3. ON 時 Apply：Offset 瞬間歸零後重新開始，跳變 ≤ 原本的 Angle。
4. OFF → ON：從 CENTER 開始，角度連續；但正弦在 CENTER 的斜率最大，所以一開始的速率就是 2π·Angle／Cycle（預設為 15.7°/s）。

- 計算：一個 10° 的跳變發生在一個 Frame 之內，在 25／50／100 Hz 下分別相當於 250／500／1000°/s。只看 Offset 本身，預設 Sweep 的峰值速率是 15.7°/s，約 56% 的時間超過 RESON 10°/s 的旗標門檻〔R1 p.63〕。預設 Angle ±10° 單獨就用掉了 NORBIT 全部 ±10° 的補償範圍〔N1 p.48〕。

### 可行做法（未排序，由使用者決定）

**A. 維持現狀，加上明確告警與記錄**
- 做法：行為不變。在 OFF、ON 時 Apply、Stale 發生時，於 UI 與事件記錄寫下時間與 Offset 的跳變量（`docs/acceptance.md` 已要求 Apply 時警告可能的 Offset 不連續）。另可讓操作員輸入下游機型的 stabilisation 上限，在 |Output Roll| 接近上限時提醒。
- 優點：送出的 Frame 不做任何平滑，符合「不可默默平滑」；跳變有上限（≤ Angle）；與「sonar 端沒有 settling 文件」的現況一致；事件記錄可供後處理標記受影響的 ping，例如搭配 CARIS 的拒收與內插設定〔C1〕。
- 缺點：MBES 仍會收到最大 Angle 的跳變；中斷期間 MBES 的行為仍然未知；超出範圍與 RESON 旗標的問題不會改變。

**B. OFF／Apply 時明確漸出（操作員可選）**
- 做法舉例：B1，OFF 時沿原本的正弦走到下一個 CENTER 才停（最多半個 Cycle，預設 2 s），Apply 也延到下一個 CENTER 才生效。B2，由操作員設定 Offset 的最大變化率或 ramp 時間。UI 要顯示「收尾中」，並保留「立即 OFF」。
- 優點：可消除情況 2、3 的角度跳變；B1 的速率不會超過 Sweep 本身。
- 缺點：OFF 不再立即生效；狀態變多，要測的也變多；對情況 1 沒有作用（Stale 時沒有新資料可送）；B2 的 ramp 速率沒有原廠數字可參考。

**C. 輸入恢復後設定明確的 hold 期（lockout）**
- 做法：Stale 恢復後 Sweep 仍維持 OFF（與現狀相同），另設一段操作員可調的 N 秒，期間不能重新開啟 Sweep；可選擇同時要求 TSS1 旗標為大寫。UI 顯示倒數與目前旗標。
- 優點：MBES 與擷取軟體會先收到一段只有 Real Roll 的資料，可用來確認 stabilisation 與告警都已恢復；做法類似 Kongsberg 校正時給 roll sensor 的 lead-in〔K4 p.44〕；檢查旗標可避免在 POS MV 本身還在 settling 時疊加 Sweep〔A1 p.3-20〕〔T1〕；不修改任何 Frame。
- 缺點：沒有原廠文件說 sonar 需要這段時間，N 該設多少沒有依據；無法消除恢復瞬間的跳變；「再從 0 開始」目前已經做到（OFF → ON 從 CENTER 開始）；若改成時間到就自動開啟 Sweep，會違反「恢復後 Sweep 維持 OFF」這項已確認的需求。

**D. 輸入恢復時，讓 Offset 從最後的值漸回 0**
- 優點：如果 MBES 在中斷期間保留了最後的值，可以消除恢復時的跳變。
- 缺點：恢復後等於短暫開著 Sweep，與已確認需求衝突，必須先明確修改需求；如果 MBES 中斷時其實是歸零或停用了 stabilisation，這段 ramp 反而會製造額外偏移；必須先用 bench test 確認中斷行為才能判斷。

**E. 改用 sonar 原生 steering**
- 優點：原廠支援；sonar 回報的角度與實際一致，不必動 motion 資料〔S1〕〔S2〕〔N1 p.43〕〔R1 p.137〕〔K4 p.118〕；Offset 不再佔用 stabilisation 的範圍。
- 缺點：文件描述的是操作員手動設定的角度，不是週期擺動；要做週期控制必須透過各廠的控制協定（例如 RESON 7k remote control），各機型不同，等於另一個專案；每次改角度仍是 ping 與 ping 之間的跳變；可行性尚未驗證。

## 不確定與待查

1. 下游 MBES 的型號與韌體尚未確定（另見 `docs/architecture.md` 的 commissioning 問題）。查到會接收 TSS1 的有 RESON 7k 系列、R2Sonic 與 NORBIT standalone；Kongsberg EM 不接收 TSS1〔K5 pp.101–102〕。所有跟機型有關的結論都要對照實際機型。
2. 中斷期間與恢復時 sonar 怎麼處理（保留、歸零、停用、告警門檻）：R2Sonic、NORBIT、RESON T-series 的操作手冊沒取得，或手冊中沒寫。
3. Sonar 是直接取最新一筆 roll，還是依時間內插或外插，內部有沒有濾波；RESON 的 Motion latency 是否會用來外插。
4. 一個 ping 接收到一半時 roll 跳變，會不會讓這個 ping 畸變（RESON 逐 sample 記錄 roll〔R1 p.62–63〕）；swath 擺動後，adaptive gate 與 range 要幾個 ping 才恢復。
5. 超出 stabilisation 範圍時，sonar 是夾在上限、停用還是告警。R2Sonic 只有搜尋摘錄〔S3〕；NORBIT 與 Kongsberg 都沒有說明。
6. 是否有 sonar 或軟體依 TSS1 旗標拒收資料；POS MV V5 如何判定旗標、settling 實際多久（「3 分鐘」出自 TSS 文件〔T1〕）。
7. R2Sonic 手冊的段落無法直接開啟（ManualsLib 回應 403），需要用手冊原件確認。
8. RESON T-series 的 roll stabilisation 角度上限。
9. NORBIT 的 TSS1 時間戳是在 GUI 電腦打上，還是轉給 SIU 才打上；這會影響 Proxy 額外加上的延遲。

建議用 bench 或海上測試取代上述推論：以 Simulator → Proxy → MBES（或 Simulator 直接接 MBES）的方式注入以下狀況。
- (a) 0.5、1、2、5、15 s 的中斷，中斷當下的 Offset 分別為 0 與 ±Angle。
- (b) 2°、5°、10° 的跳變。
- (c) 旗標為小寫的 Frame。
- (d) |Output Roll| 超過機型上限。

測試時記錄 sonar 回報的 stabilisation 角度（RESON 7012／7004、NORBIT 的 Roll Stab 燈與 wedge、R2Sonic 的輸出資料）、每個 ping 的有效 detection 數，以及擷取軟體的告警。

## 來源

| 代號 | 文件（章節） | 廠商 | 版本／日期 | URL |
|---|---|---|---|---|
| A1 | POS MV V4 Installation and Operation Guide（Table 20 TSS1 Output Format p.3-20–3-21；Heave Filter p.4-21；TrueHeave p.6-2；Filter settling time p.B-15） | Applanix | PUBS-MAN-000047 Rev 1，2005 | https://downloads.psl.noaa.gov/psd3/cruises/WHOTS_2009/KiloMoana/Other%20instruments/POSMV%20infos/POSMV-V4-OperatorGuide.pdf |
| A2 | POS MV V4 User ICD（Table 74 Binary Port Definition, pp.97–98） | Applanix | PUBS-ICD-000551 Rev 0.0，2006-05-12 | https://downloads.psl.noaa.gov/psd3/cruises/WHOTS_2009/KiloMoana/Other%20instruments/POSMV%20infos/POSMV-V4-UserManual.pdf |
| T1 | UHDAS「3.4.2.4 MAHRS Diagnostics」中的 TSS1 status flag 定義（轉載，未註明原始出處；MAHRS 為 TSS 產品） | University of Hawaii（內容源自 Teledyne TSS） | 取用 2026-10-02 | https://currents.soest.hawaii.edu/docs/adcp_doc/UHDAS_OPERATIONS/troubleshooting/serial_nmea/mahrs_diagnostics.html |
| T2 | sbgECom Library「Third party messages」（TSS1、Simrad 3000） | SBG Systems | 5.8.935-stable | https://developer.sbg-systems.com/sbgECom/5.8/md_doc_2third_party_messages.html |
| T3 | OEM7 Documentation「TSS1」log（Message ID 1456） | NovAtel | 線上版，取用 2026-10-02 | https://docs.novatel.com/OEM7/Content/SPAN_Logs/TSS1.htm |
| K1 | EM 2040 Product Description（pp.9–10, 47, 51） | Kongsberg Maritime | 344245/D，2012-02 | https://cdn1.shipserv.com/ShipServ/pages/profiles/54162/documents/EM-2040-Product-Description.pdf |
| K2 | EM 712 datasheet | Kongsberg Maritime | 387595/C，2022-03 | https://www.kongsberg.com/contentassets/2c197a1f0a33461b9642276e31b67fb7/datasheet_em_712.pdf |
| K3 | EM Series Datagram formats（Attitude p.21；EM Attitude input format p.23；p.39；Attitude datagram pp.66–67；Installation parameters p.83） | Kongsberg Maritime | 850-160692/W，2018-01 | https://www.kongsberg.com/contentassets/098bb8dd6793498c8deb72431583958e/160692_em_datagram_formats.pdf |
| K4 | SIS Reference Manual 5.15（Clock setup p.24；Time stamp pp.26–27；Roll calibration p.44；Runtime Survey p.118；Attitude systems p.139；Sensor options p.175） | Kongsberg Discovery | 429004/K，2026-08-14 | https://www.kongsbergdiscovery.online/sis/ref/sis_ref_en_a4.pdf |
| K5 | EM 2040P Installation manual（Interface specifications pp.99–102） | Kongsberg Maritime | 417418/C，2020-08 | https://www.kongsberg.com/contentassets/bde3261d49644a8f8b40c5b0f08b8df9/417418ac_em2040p_installation_manual_en.pdf |
| R1 | 7k Data Format, Volume I（9 Time Tagging p.17；1012 p.34；1016 p.36；7000 p.43；7004 p.53；7012 pp.62–63；7017 p.65；1037 p.136；1038 p.137） | Teledyne RESON | Version 2.41，2015-08-17 | https://www.ldeo.columbia.edu/res/pi/MB-System/mbweb/formatdoc/Reson/Reson7kDataFormatVolumeIVersion2.41.pdf |
| R2 | SeaBat T50-S Product Leaflet | Teledyne RESON | © 2026 | https://teledynemarine.com/en-us/products/SiteAssets/RESON/SeaBat%20T50-S%20Product%20Leaflet.pdf |
| S1 | Resources: Frequently Asked Questions | R2Sonic | 線上版，取用 2026-10-02 | https://r2sonic.com/resources/faq/ |
| S2 | Sonic 2024 brochure | R2Sonic | 未標日期（經銷商 2023-09 上傳） | https://echo81.com/wp-content/uploads/2023/09/R2Sonic-2024-Brochure.pdf |
| S3 | R2Sonic 2022 Operation Manual p.49「Roll Stabilize」（只取得搜尋索引摘錄；頁面回應 HTTP 403） | R2Sonic（ManualsLib 收錄） | 版本未知 | https://www.manualslib.com/manual/1570458/R2sonic-2022.html?page=49 |
| N1 | Multibeam Sonar User and Technical Manual（pp.24, 40, 41, 43, 48, 55） | NORBIT Subsea | TN-140075-11，2020-09（GUI 10.4.8／FW 5.2.1） | https://irp.cdn-website.com/5409427c/files/uploaded/TN-140075-11_NORBIT_Sonar_User_Manual.pdf |
| C1 | HIPS and SIPS Help「Filter HIPS Attitude」 | Teledyne CARIS | 8.0 | https://docs.teledynecaris.com/docs/8.0/hips%20and%20sips/CARIS%20HIPS%20and%20SIPS%20Help/FilterHIPSAttitude.html |
| C2 | HIPS and SIPS Help「Import Auxiliary Data」 | Teledyne CARIS | 4.4.10 | https://docs.teledynecaris.com/docs/4.4.10/hips%20and%20sips/CARIS%20HIPS%20and%20SIPS%20Help/ImportHIPSFromAuxiliaryNAVLAB.html |
| H1 | HYPACK User Manual（Hardware Connect p.2-209；Offsets and Latency p.2-219；HYSWEEP alarms p.3-157） | HYPACK | Last Updated 2016-02 | https://www.ldeo.columbia.edu/res/pi/MB-System/mbweb/formatdoc/HYPACK/2016HYPACKUserManual.pdf |
| Q1 | Qinsy「How-to Quality Management System」 | QPS | 線上版，取用 2026-10-02 | https://qpssoftware.scrollhelp.site/qinsy/how-to-quality-management-system |
| Q2 | Qimera「Processing」 | QPS | 線上版，取用 2026-10-02 | https://qpssoftware.scrollhelp.site/qimera/qimera-processing |

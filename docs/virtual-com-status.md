# Virtual COM 環境管理現況

術語見 [GLOSSARY.md](../GLOSSARY.md)。

## 結構

- **判斷規則**（`src/environment/virtual_com.*`，不呼叫作業系統）：配號，建立、移除、解除安裝的判斷，TX／Input／Output／RX 角色對應，擁有權記錄格式，以及舊版記錄匯入。CTest `virtual_com` 有 16 項行為測試。
- **Windows 端**（`src/platform/windows/virtual_com_worker.cpp`）：以 `posmv-simulator env <動作> --result FILE --legacy FILE` 執行。動作分兩類：
  - 不需要 UAC：`inspect`、`test`
  - 經 UAC 提升權限：`install`、`create`、`remove`、`uninstall`
- **Simulator**：`EnvironmentManager` 啟動上述工作程式並等它結束，再讀 `result.txt`（`status=`、`message=`、`roles=` 三種行）。Simulator 開啟時自動執行一次 `inspect`，並以醒目字體顯示正式程式要選的兩個埠與鮑率。舊的 PowerShell 管理腳本已移除。

## 驅動

- 固定使用 com0com 2.2.2.0：`com0com-2.2.2.0-x64-fre-signed.zip`，SHA-256 `3bc7313e8577774ce78cff4d1c816faf96cfa48c485bbcc5527d4569f7d5e373`。3.0.0.0 在開啟 Secure Boot 的 Windows 上無法載入，原因見 [2026-10-01-com0com-2.2.2.0.md](verification/2026-10-01-com0com-2.2.2.0.md)。
- 安裝檔依序使用：
  1. 執行檔旁 `drivers\` 資料夾裡的副本（可攜 ZIP 內附）
  2. `%ProgramData%\POSMV-Sweep\virtual-com\` 裡先前下載的副本
  3. 從 SourceForge 下載

  每個來源都必須 SHA-256 相符才會使用，不符的副本會略過並在結果中說明。
- 電腦上已經有任何版本的 com0com 時，不會重新安裝。
- 不關閉、也不引導關閉任何 Windows 安全機制。

## 配對

- 維持兩組本程式配對。COM 號碼從 COM10 開始往上挑，跳過三種號碼：Windows COM 號碼資料庫（ComDB）已保留的、目前已存在（SERIALCOMM）的、其他配對正在使用的。
- 建立時開啟鮑率模擬（EmuBR）與溢位模擬（EmuOverrun），讓虛擬線路的行為接近實體串列線。
- 第一組配對是 Simulator TX → 正式程式 Input，第二組是正式程式 Output → Simulator RX。
- 配對雖已登記、但 COM 埠沒有出現（例如驅動被擋，裝置管理員代碼 52）時，不提供角色對應。

## 擁有權

- 記錄檔：`%ProgramData%\POSMV-Sweep\virtual-com\ownership.txt`，整台電腦共用，只有提升權限的動作會寫入。
- 第一次執行提升權限的動作時，會匯入舊版 PowerShell 留下的 `%LOCALAPPDATA%\POSMV-Sweep\virtual-com\owned.json`，並把驅動視為本程式安裝。
- 本程式配對的埠名稱被改過時，不做任何變更。
- 建立配對後先寫入擁有權，再檢查 COM 埠；這樣即使驅動被擋，之後仍能用「移除」清掉。

## 解除安裝

- 只有在驅動是本程式安裝的、而且電腦上沒有其他程式的配對時才允許。
- 流程：先移除本程式配對，再執行 com0com 的 `uninstall.exe /S`。如果驅動服務仍在，提示重新開機。

## 驗證狀態（2026-10-01，本機）

- 已通過：CTest 7/7，含 `virtual_com`；以一般權限執行 `inspect` 與 `test`，四個方向的雙向自測都通過，每個方向約 15 ms。
- 尚未實測：新程式需要 UAC 的 `install`、`create`、`remove`、`uninstall`。
- 風險：Microsoft 正逐步取消對交叉簽章驅動的信任，2.2.2.0 之後可能被擋。

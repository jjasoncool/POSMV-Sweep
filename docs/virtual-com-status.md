# Virtual COM 環境管理現況

術語見 [GLOSSARY.md](../GLOSSARY.md)。

## 結構

- **判斷規則**（`src/environment/virtual_com.*`，不呼叫作業系統）：配號，建立、移除、解除安裝的判斷，TX／Input／Output／RX 角色對應，擁有權記錄格式，以及舊版記錄匯入。CTest `virtual_com` 有 25 項行為測試。配對是否存在以匯流排裝置的 `PortNum` 為準，不看殘留的埠設定。
- **Windows 端**（`src/platform/windows/virtual_com_worker.cpp`）：以 `posmv-simulator env <動作> --result FILE --legacy FILE` 執行。動作分兩類：
  - 不需要 UAC：`inspect`、`test`
  - 經 UAC 提升權限：`install`、`create`、`remove`、`uninstall`
- **Simulator**：`EnvironmentManager` 啟動上述工作程式並等它結束，再讀 `result.txt`。結果檔有六種行：`status=`、`message=`、`roles=`、`driver=`（已安裝的版本）、`test=`（自測結果：`passed`、`busy`、`failed`），以及 `actions=`（目前合理的動作）。
  - Simulator 開啟時自動執行 `inspect`。環境就緒時（開啟時、安裝或建立之後）接著自動自測。
  - 兩頁的環境按鈕都只在 `actions=` 列出時出現（`available_actions`），「重新檢查」一直顯示：

    | 狀態 | 出現的按鈕 |
    |---|---|
    | 驅動未安裝 | 安裝 |
    | 驅動已安裝、沒有本程式配對 | 建立；驅動是本程式裝的、也沒有其他程式的配對時，再加解除安裝 |
    | 少一組配對 | 建立（補建）、移除 |
    | 配對已登記但埠沒出現 | 移除 |
    | 就緒 | 雙向自測、移除（解除安裝要先移除配對） |
    | 本程式配對被改名 | 無 |

  - 「測試」頁只有一行狀態，只放安裝與建立兩個按鈕；其餘管理動作在「環境與進階」頁。
  - 以醒目字體顯示正式程式要選的兩個埠與鮑率。
  - 正式程式佔用埠時，自測回報 `busy`，提示先中斷連線。
  - 舊的 PowerShell 管理腳本已移除。

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
- 流程：
  1. 移除本程式配對。
  2. 以 `SetupUninstallOEMInfW` 把 com0com 套件從 Windows 驅動程式庫移除。2.2.2.0 自己的解除安裝程式會留下孤兒套件，所以這一步要先做。
  3. 執行 com0com 的 `uninstall.exe /S`。

  如果驅動服務仍在，提示重新開機。

## 驗證狀態（2026-10-02，本機）

- 已通過：`virtual_com` 共 25 項行為測試；`remove`、`uninstall`、`install`、`create`、`test` 的完整生命週期實測，詳見 [2026-10-02-virtual-com-lifecycle.md](verification/2026-10-02-virtual-com-lifecycle.md)。Simulator 開啟時的自動檢查與自測，以及正式程式佔用埠時的 `busy` 提示，也已在畫面上確認。
- 尚未實測：透過 Simulator 按鈕的 UAC 路徑與取消 UAC；有其他程式配對或驅動被擋時的實機情境。
- 風險：Microsoft 正逐步取消對交叉簽章驅動的信任，2.2.2.0 之後可能被擋。

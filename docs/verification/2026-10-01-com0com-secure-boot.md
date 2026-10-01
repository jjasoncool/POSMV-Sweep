# com0com 3.0.0.0 在 Windows 11 24H2 無法載入（2026-10-01）

## 環境

- Windows 11 24H2，build 26100.8875
- Secure Boot：開啟（`UEFISecureBootEnabled=1`）
- 易受攻擊驅動封鎖清單：開啟（`VulnerableDriverBlocklistEnable=1`）
- 記憶體完整性（HVCI）：已設定、未執行（`Win32_DeviceGuard.SecurityServicesRunning=0`）
- 驅動：`com0com-3.0.0.0-i386-and-x64-signed.zip` 內的 `Setup_com0com_v3.0.0.0_W7_x64_signed.exe`，先前以 `/S` 安裝到 `C:\Program Files (x86)\com0com`
- `com0com.sys`、`com0com.cat`、`setupc.exe` 的 Authenticode 簽章者：CyberCircuits（憑證 2018-01-27 到期，COMODO SHA-1 時間戳記）
- Driver store：`oem75.inf`（com0com.inf）、`oem82.inf`（cncport.inf），Driver Version 06/04/2012 3.0.0.0
- 測試前既有 COM：COM1
- 程式版本：`d5eee18`，加上本次的 `setupc` 工作目錄修正

## 步驟與結果

1. 以 UAC 提升權限執行 `Manage-VirtualCom.ps1 -Action Create`：失敗。`setupc` 到 `C:\WINDOWS\system32` 找 `com0com.inf`，回報 ERROR 2（找不到檔案）。原因是提升權限的程序預設工作目錄為 System32，而 `setupc` 從工作目錄讀取 INF。已改為在 `setupc` 所在資料夾執行。
2. 修正後重跑 Create：`setupc install 0 PortName=COM# PortName=COM#` 建立了 `ROOT\COM0COM\0000`，但裝置狀態為 Error、`CM_PROB_UNSIGNED_DRIVER`（裝置管理員代碼 52）。`com0com` 服務已安裝但停在 Stopped，沒有出現新的 COM 埠。腳本回報 `Pair 0 did not expose two COM names`。
3. 系統事件（同為 15:58:00）：
   - CodeIntegrity/Operational 3004：Windows is unable to verify the image integrity of the file `\Device\HarddiskVolume3\Windows\System32\drivers\com0com.sys` because file hash could not be found on the system.
   - System，Kernel-PnP 219：The driver `\Driver\com0com` failed to load.
   - System，Service Control Manager 7045：A service was installed in the system.
4. 清除：以提升權限執行 `setupc remove 0`，裝置移除成功，COM 埠回到只有 COM1。`com0com` 服務（Stopped）、安裝資料夾與 driver store 套件仍保留。

## 結論

- 這個簽章版本的 com0com 在開啟 Secure Boot 的 Windows 11 24H2 上無法載入。依既定原則不關閉任何 Windows 安全機制；virtual COM 相關驗收標為受阻，改評估其他驅動或硬體方案。
- `Install` 回報 exit code 0，不代表驅動能夠載入。

## 同時發現的問題

- 建立配對失敗時，腳本沒有把已建立的配對寫進擁有權記錄，之後無法用 Remove 清除。這次改用 `setupc remove 0` 手動清除。
- Simulator 以 `powershell -File` 啟動腳本，沒有指定執行原則。在執行原則為 Restricted 的電腦，或是從網路下載後解壓、帶有網路標記的 ZIP（RemoteSigned）上，腳本會無法執行。這台的 CurrentUser 原則是 Bypass，所以這次沒有遇到。

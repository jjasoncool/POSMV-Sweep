# Simulator Virtual COM 管理：完整生命週期實測（2026-10-02）

環境同 [2026-10-01-com0com-2.2.2.0.md](2026-10-01-com0com-2.2.2.0.md)：Windows 11 24H2 build 26100.8875，Secure Boot 開啟。程式版本為 `d4d72e2`，加上本報告提到的兩個修正。

## 方法

以一個提升權限的 PowerShell（一次 UAC）依序執行 `posmv-simulator env <動作> --result FILE --legacy FILE`；每個動作之後記錄：

- com0com 裝置數與狀態
- 驅動服務
- Windows 驅動程式庫中的 com0com 套件
- `setupc.exe` 位置
- COM 埠
- ComDB 前四個 byte

`inspect` 與 `test` 則以一般權限執行。測試前，正式程式與 Simulator 都沒有在執行。

## 發現並修正的問題

1. **移除配對後仍「看得到」配對**：`setupc remove` 會刪除裝置，但留下 `Services\com0com\Parameters\CNCA<n>/CNCB<n>` 的埠設定。程式原本用這些設定判斷配對是否存在，結果把已移除的配對當成其他程式的配對，解除安裝因此被拒絕。
   - 修正：改以匯流排裝置（`Enum\ROOT\CNCPORTS`，3.x 為 `Enum\ROOT\COM0COM`）的 `Device Parameters\PortNum` 判斷配對是否存在。依據是 com0com 原始碼的 `setup/portnum.cpp`。驅動被擋時，匯流排裝置仍在，所以仍能移除。
   - 新增測試：`parameters left by a removed pair are not a Virtual Pair`。
2. **解除安裝後，驅動程式庫留下套件 `oem75.inf`**：2.2.2.0 的解除安裝程式會直接刪除 `%windir%\INF\oemNN.inf`，沒有呼叫 `SetupUninstallOEMInf`；這個 API 是 3.0 才加入的。結果套件變成孤兒，留在驅動程式庫裡。
   - 修正：在執行 com0com 的解除安裝程式之前，先以 `SetupUninstallOEMInfW`（不強制）移除符合 com0com 裝置類別 GUID 與發行者的 `oem*.inf`。

## 修正後的最終結果

| 步驟 | 結果 | 之後的狀態 |
|---|---|---|
| uninstall | exit 0；「已從 Windows 驅動程式庫移除 oem75.inf」、「已解除安裝 com0com」 | 裝置 0；服務不存在；驅動程式庫沒有 com0com；`setupc` 不存在；COM1；ComDB `01 00 00 00` |
| install | exit 0；安裝檔來源為先前下載，SHA-256 相符 | `setupc` 存在；驅動程式庫要到建立配對時才加入套件 |
| create | exit 0；建立 COM10↔COM11、COM12↔COM13，角色 TX=COM10、Input=COM11、Output=COM12、RX=COM13 | 6 個裝置全部 OK；服務 Running；COM1、COM10–13；ComDB `01 1E 00 00` |
| test（一般權限） | exit 0；四個方向都通過，15.3–15.7 ms | — |

修正前的同一輪也驗證了兩件事：

- 第一次執行的 `remove` 自動匯入了舊版 `owned.json`，之後寫入 `%ProgramData%\POSMV-Sweep\virtual-com\ownership.txt`。
- `install` 從 SourceForge 下載，SHA-256 相符後才安裝。

## 尚未涵蓋

- 透過 Simulator 按鈕操作的路徑（`EnvironmentManager` 以 `runas` 啟動），以及取消 UAC 的情況。
- 電腦上有其他程式配對、或驅動被擋（代碼 52）的實機情境；這兩種只有單元測試。
- 電腦上已經裝了 com0com 3.x 時執行安裝。

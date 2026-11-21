# 音訊緩衝回歸測試

文件協調器可獨立驗證，不需編譯原生 App：
`python -m unittest discover -s Testing -p test_api_docs.py`。
涵蓋唯讀檢查、內容與時間戳差異、產物缺失、標籤相依、失敗保留、替換還原、
API 宣告差異、輸出邊界、併發鎖、快取與短暫存取拒絕重試。fixture 保留在忽略的 `build/api-docs-test-*`。

整輪入口：`python Testing/run_tests.py`，預設沿用 `build/dev/CMakeCache.txt`
的工具鏈，也可指定 `--prefix <UCRT64 路徑>`。測試執行檔統一放在
`build/Testing`，主程式仍使用 `build/dev/uxplay-windows.exe` 與 VSCode 偵錯。

日誌測試刻意阻塞主控台讀取，驗證多執行緒產生日誌不等待 I/O、佇列容量限制、
溢位摘要、錯誤優先保留、flush 逾時、關閉排空、檔案輪替與重新安裝。
日誌 fixture 保留在忽略的 `build/logging-test-*`，供需要時檢查。

`hud_overlay_test.cpp` 使用真正的 Win32 HWND 與隱藏的影片 owner，驗證
50 輪重複附著、換 owner、顯示旗標、解除附著與 owner 銷毀後的 timer／視窗回收。
此測試不顯示影片，不代表 D3D11 串流重疊時的呈現或視覺定位已驗收。

`settings_file_test.cpp` 直接測試正式使用的 `settingsfile::writeAtomically`，
涵蓋初次建立、更新、未變更不重寫、外部讀取占用時保留原檔、解除占用後重試、
CRLF 相容及 open 失敗。測試副本保留於 `build/settings-file-test-*`，不碰 AppData。
`settings_commit_probe.cpp` 是原始自我占用的重現工具，不代表正式儲存函式；
要確認修正請執行前者或整輪入口。

在專案根目錄的 MSYS2 UCRT64 shell 執行：

```sh
gcc -Wall -Wextra -Ilibuxplay/lib Testing/audio_buffer_test.c libuxplay/lib/byteutils.c -lws2_32 -lpthread -o build/audio_buffer_test.exe
./build/audio_buffer_test.exe
gcc -O2 -flto -Ilibuxplay/lib Testing/audio_delivery_test.c libuxplay/lib/byteutils.c -lws2_32 -lpthread -o build/audio_delivery_test.exe
./build/audio_delivery_test.exe
```

測試直接使用目前的 `raop_buffer.c`，涵蓋重傳等待、重複呼叫不提前跳包、
連續缺包、序號回繞、停用重傳、重複／過期封包，以及恢復時保留已收到的資料。
AES 使用替身，這些測試不驗證加解密、AAC 解碼或實際音效裝置。

另以可控制的單調時鐘驗證 349／350 ms 邊界、重傳節流與明確無內容封包。
`audio_delivery_test.c` 使用真正的 RTP 接收迴圈與緩衝，模擬 socket 事件，
確認只有控制通道重傳回覆時仍會排空，以及只有 select timeout 時仍可恢復。

缺包等待以 32 個序號距離或 350 ms 的先到條件為限；每 5 ms 的接收 timeout
也會嘗試取出音訊，同一缺口的重傳要求至少間隔 20 ms。`gap_skips` 現在計算
實際跳過的缺失序號數；明確無內容封包不算缺包。`last_gap_ms` 是最後跳包時
已收到後續封包的等待時間，不再是三秒備援重啟門檻。

一般引擎 log 每秒有兩類時間診斷，不需要封包級 Debug：

- `audio ingress ... mapped_age_ms`：一般封包抵達並被接受時，與目前映射的
  音訊時間戳相差多少毫秒；包含傳送端與網路時間，也可能包含時鐘映射誤差。
- `audio delivery ... mapped_age_ms ... queue_wait_ms`：送交 renderer 前的
  時間戳落後量，以及同一封包在本機緩衝停留的單調時鐘時間。

若 ingress 已落後約 13000 ms，而 queue_wait 很小，應往傳送端、網路或
RTP／NTP 映射追查；若 queue_wait 明顯增加，應往本機佇列與執行緒排程追查。
這些數字都不是喇叭實際播放延遲。

音訊診斷的 `dequeued` 表示從 RTP 緩衝取出的封包數，不是 AAC 解碼成功數。
`pushed` 是自音訊啟動後 appsrc 接受的封包數，flush 不再清零。
`output_age` 是距最後一次 level 訊息的時間，以 ms、s 或 min 顯示；
「尚無輸出資料」表示尚未取得觀測，不是超長延遲。level 位於音效 sink 之前，
因此這個欄位不代表喇叭的播放延遲或實際出聲證明。

實機驗收仍需讓 iPad 持續播放有聲內容，確認音訊連續性、缺包恢復與輸出觀測；
編譯及緩衝測試通過不代表實機音訊問題已解決。

## GPU 探測與執行緒回收

在相同 UCRT64 shell 執行：

```sh
g++ -std=c++17 Testing/gpu_probe_test.cpp $(pkg-config --cflags --libs Qt6Core) -ld3d11 -ldxgi -ldxguid -luuid -o build/gpu_probe_test.exe
./build/gpu_probe_test.exe
gcc -O2 -flto -ffunction-sections -fdata-sections -Ilibuxplay/lib Testing/worker_cleanup_test.c -Wl,--gc-sections -lws2_32 -lpthread -o build/rtp_cleanup_test.exe
./build/rtp_cleanup_test.exe
gcc -DTEST_NTP -O2 -flto -ffunction-sections -fdata-sections -Ilibuxplay/lib Testing/worker_cleanup_test.c -Wl,--gc-sections -lws2_32 -lpthread -o build/ntp_cleanup_test.exe
./build/ntp_cleanup_test.exe
```

GPU 測試使用真正的 D3D11 裝置探測並計數，確認首次探測後，1000 輪
診斷讀取及選卡不再建立探測裝置。需要 Windows 與至少一張硬體 GPU。
裝置清單在同一程序內共用快照，變更 GPU 後需重新啟動程式。

回收測試分別使用真正的 RTP／NTP stop 函式與 pthread，模擬 running 已為 0
但尚未 join 的工作執行緒，反覆驗證 100 輪回收及重複停止。無關網路程式碼
由 LTO 移除；測試不模擬完整連線，也不代表整個 App 已通過長時間洩漏測試。

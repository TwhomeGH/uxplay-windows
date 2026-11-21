#!/usr/bin/env python3
"""Fill the zh_TW Qt Linguist catalog from the maintained English→zh_TW map.

This script is a source-controlled translation helper, not a build artifact.
Run it from any directory after `lupdate`; it resolves the catalog relative to
the repository root.
"""
import xml.etree.ElementTree as ET
from pathlib import Path

translations = {
    "UsbDeviceConnector": {
        "Python 3 with pymobiledevice3 was not found. Set UXPLAY_USB_PYTHON to a Python interpreter.":
            "找不到 Python 3 與 pymobiledevice3。請將 UXPLAY_USB_PYTHON 設為 Python 解譯器。",
        "Checking USB device...": "正在檢查 USB 裝置…",
        "Could not query usbmux. ": "無法查詢 usbmux。",
        "usbmux returned invalid JSON.": "usbmux 回傳無效的 JSON。",
        "No USB device found. Connect and unlock the iPad, then press Connect again.":
            "找不到 USB 裝置。請連接並解鎖 iPad，再按一次連線。",
        "Found %1; checking pairing...": "找到 %1；正在檢查配對…",
        "Paired; checking Developer Mode...": "已配對；正在檢查開發者模式…",
        "Tap Trust This Computer on the iPad...": "請在 iPad 上點選「信任此電腦」…",
        "Pairing failed. Unlock the iPad, tap Trust This Computer, then press Connect again. ":
            "配對失敗。請解鎖 iPad、點選「信任此電腦」，再按一次連線。",
        "Could not read Developer Mode status. ": "無法讀取開發者模式狀態。",
        "Developer Mode is off. Enable it in Settings > Privacy & Security > Developer Mode, reboot, then press Connect again.":
            "開發者模式未開啟。請在「設定 > 隱私權與安全性 > 開發者模式」開啟、重新開機，再按一次連線。",
        "Developer Mode on; mounting the DDI...": "開發者模式已開啟；正在掛載 DDI…",
        "Developer Disk Image mount failed. ": "開發者磁碟映像（DDI）掛載失敗。",
        "Reading battery and power...": "正在讀取電池與供電資訊…",
        "USB ready: %1": "USB 就緒：%1",
        "charging": "充電中",
        "not charging": "未充電",
        "%1W adapter": "%1W 變壓器",
        "USB check cancelled": "USB 檢查已取消",
    },
    "UsbObsController": {
        "Timed out waiting for the USB screen stream": "等待 USB 螢幕串流逾時",
        "Python was not found. Set UXPLAY_USB_PYTHON to a Python 3 interpreter.":
            "找不到 Python。請將 UXPLAY_USB_PYTHON 設為 Python 3 解譯器。",
        "stream_to_tcp.py was not found next to the executable or in the source tree.":
            "在執行檔旁或原始碼樹中找不到 stream_to_tcp.py。",
        "Could not allocate local ports for the USB Clean Output.":
            "無法為 USB 乾淨輸出配置本機連接埠。",
        "USB sidecar failed to start (%1)": "USB sidecar 啟動失敗（%1）",
        "USB sidecar exited (code %1)": "USB sidecar 已結束（代碼 %1）",
        "USB sidecar did not start: %1": "USB sidecar 未啟動：%1",
        "USB bridge failed to start (%1)": "USB bridge 啟動失敗（%1）",
        "USB bridge exited (code %1)": "USB bridge 已結束（代碼 %1）",
        "USB bridge did not start: %1": "USB bridge 未啟動：%1",
        "USB pipeline is missing a required element": "USB 管線缺少必要元素",
        "d3d11videosink does not support VideoOverlay": "d3d11videosink 不支援 VideoOverlay",
        "GStreamer could not start the USB pipeline": "GStreamer 無法啟動 USB 管線",
        "USB stream ended": "USB 串流已結束",
    },
    "CleanOutputWindow": {
        "Always on top": "永遠置頂",
        "Copy OBS source name": "複製 OBS 來源名稱",
        "Copied: %1": "已複製：%1",
        "Move window": "移動視窗",
        "Resize window": "調整視窗大小",
        "Reset size": "重設大小",
        "Close": "關閉",
    },
    "DiagnosticsExportDialog": {
        "Save Diagnostics": "儲存診斷",
        "Export one incident window, not the entire history.\nData is written locally and is never uploaded. A folder is the default; ZIP is only for sharing.":
            "匯出單一事件區間，不是完整歷史。\n資料只寫在本機，不會上傳。預設輸出資料夾；只有要分享時才建立 ZIP。",
        "Since last AirPlay connection": "自上次 AirPlay 連線起",
        "Last 1 hour": "最近 1 小時",
        "Last 5 hours": "最近 5 小時",
        "Entire app session": "本次 App 工作階段",
        "Custom range...": "自訂區間...",
        "Create .zip for sharing (optional)": "建立 ZIP 供分享（選用）",
        "Range": "區間",
        "From": "開始",
        "To": "結束",
        "From must be earlier than To.": "開始時間必須早於結束時間。",
        "Custom window: %1 → %2": "自訂區間：%1 → %2",
        "%1: %2 → %3": "%1：%2 → %3",
    },
    "MainWindow": {
        "uxplay-windows": "uxplay-windows",
        "Settings": "設定",
        "Edit extra_args in settings.json, then restart to apply.":
            "在 settings.json 編輯 extra_args，重新啟動後套用。",
        "Client: -": "用戶端：-",
        "Connections: 0": "連線數：0",
        "Status: Not connected": "狀態：未連線",
        "Video Codec: -": "視訊編碼：-",
        "Video Decoder: -": "視訊解碼器：-",
        "Video Renderer: -": "視訊 Renderer：-",
        "Stream Size: -": "串流尺寸：-",
        "Audio Codec: -": "音訊編碼：-",
        "Audio Format: -": "音訊格式：-",
        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)":
            "請重新啟動 uxplay-windows 以套用變更。\n（右鍵點擊托盤圖示）",
        "Engine settings changed. Apply to restart the engine.":
            "引擎設定已變更，套用後重新啟動引擎。",
        "Apply & restart engine": "套用並重新啟動引擎",
        "AirPlay name saved. Apply engine changes to use it.":
            "已儲存 AirPlay 名稱，套用引擎變更後生效。",
        "HEVC needs height above 1080p; this resolution will use H.264.":
            "HEVC 需要高度高於 1080p；此解析度會使用 H.264。",
        "Video codec preference saved. Restart to apply.":
            "已儲存視訊編碼偏好，重新啟動後套用。",
        "H.264 only: HEVC is not advertised to the client.":
            "僅 H.264：不會向用戶端宣告 HEVC。",
        "HEVC advertised; the client may still choose H.264.":
            "已宣告 HEVC；用戶端仍可能選擇 H.264。",
        "Status: %1": "狀態：%1",
        "Status: Streaming": "狀態：串流中",
        "Video Codec: %1": "視訊編碼：%1",
        "Video Decoder: %1": "視訊解碼器：%1",
        "Video Renderer: %1": "視訊 Renderer：%1",
        "Stream Size: %1": "串流尺寸：%1",
        "Audio Codec: %1": "音訊編碼：%1",
        "Audio Format: %1": "音訊格式：%1",
        "Client: %1": "用戶端：%1",
        "unknown": "未知",
        "Status: Connected": "狀態：已連線",
        "Device connected": "裝置已連線",
        "A device connected": "有裝置已連線",
        "Device disconnected": "裝置已中斷",
        "Not connected": "未連線",
        "A device": "有裝置",
        "Connection rejected": "連線被拒絕",
        "Connections: %1": "連線數：%1",
        "Enter a name for this AirPlay receiver.":
            "請輸入此 AirPlay 接收器的名稱。",
        "Restarting uxplay-windows with the new AirPlay name.":
            "正在使用新的 AirPlay 名稱重新啟動 uxplay-windows。",
        "Waiting for stream": "等待串流",
        "Starting AirPlay": "正在啟動 AirPlay",
        "Stopping AirPlay": "正在停止 AirPlay",
        "Stop USB Clean Output": "停止 USB 乾淨輸出",
        "USB Clean Output started. Add it in OBS with Window Capture.":
            "USB 乾淨輸出已啟動。請在 OBS 以視窗擷取加入。",
        "Cancel USB check": "取消 USB 檢查",
        "\nUSB uses the iPad's only port. Unplug to charge, or use a PD dock.":
            "\nUSB 佔用 iPad 唯一的連接埠。若要充電請拔除，或使用 PD 擴充座。",
        "Reconnect USB device": "重新連線 USB 裝置",
        "iPad is not charging (%1W adapter); USB uses its only port. Unplug to charge, or use a PD dock.":
            "iPad 未充電（%1W 變壓器）；USB 佔用其唯一連接埠。若要充電請拔除，或使用 PD 擴充座。",
        "iPad battery is low (%1%).": "iPad 電量偏低（%1%）。",
        "USB ready: %1": "USB 就緒：%1",
        "Retry USB connection": "重試 USB 連線",
        "Virtual camera is not implemented yet. Use Clean window for now.":
            "虛擬攝影機尚未實作，請先使用乾淨視窗。",
        "Rebuilding AirPlay stream...": "正在重建 AirPlay 串流…",
        "Client": "用戶端",
        "Status": "狀態",
        "Video codec": "視訊編碼",
        "Output size": "輸出尺寸",
        "Decoder": "解碼器",
        "Renderer": "Renderer",
        "FPS": "FPS",
        "Audio": "音訊",
        "Show all": "顯示全部",
        "Client: ": "用戶端：",
        "Status: ": "狀態：",
        "Video": "視訊",
        "Video Codec: ": "視訊編碼：",
        "waiting": "等待中",
        "Output": "輸出",
        "%1  (requested %2)": "%1（請求 %2）",
        "Video Decoder: ": "視訊解碼器：",
        "Video Renderer: ": "視訊 Renderer：",
        "%1 (%2, age %3 ms)": "%1（%2，frame age %3 ms）",
        "Audio Codec: ": "音訊編碼：",
        "Output: %1   (requested %2)": "輸出：%1（請求 %2）",
        "Since last AirPlay connection": "自上次 AirPlay 連線起",
        "Last 1 hour": "最近 1 小時",
        "Could not save diagnostics.": "無法儲存診斷。",
        "Diagnostics": "診斷",
        "Diagnostics saved": "診斷已儲存",
        "Recent diagnostics were saved.": "已儲存近期診斷。",
        "The bundle is generated locally and redacted, but review it before sharing. It contains logs, settings, events, metrics and system information.":
            "此診斷包在本機產生並已遮蔽敏感資訊，但分享前仍請自行檢查。內容包含日誌、設定、事件、指標與系統資訊。",
        "Open folder": "開啟資料夾",
        "Copy path": "複製路徑",
        "Windows woke from sleep. Restarting to refresh AirPlay discovery.":
            "Windows 已從睡眠喚醒，正在重新啟動以更新 AirPlay 探索。",
        "Bonjour Service Not Running": "Bonjour 服務未執行",
        "Bonjour Service is installed but could not be started.\n\nOpen Services (services.msc), start 'Bonjour Service', then relaunch uxplay-windows.":
            "Bonjour 服務已安裝但無法啟動。\n\n請開啟服務（services.msc），啟動「Bonjour Service」，然後重新啟動 uxplay-windows。",
        "Bonjour Service Required": "需要 Bonjour 服務",
        "Bonjour Service is required for discovery (mDNS). It is not installed.\n\nDo you want to install it now?":
            "AirPlay 探索（mDNS）需要 Bonjour 服務，但目前未安裝。\n\n要立即安裝嗎？",
        "Bonjour Service Missing": "缺少 Bonjour 服務",
        "Bonjour Service is required. The application will now exit.":
            "需要 Bonjour 服務，應用程式即將結束。",
        "Installing Bonjour Service": "正在安裝 Bonjour 服務",
        "Starting installation of 'Bonjour Service'. Follow any UAC prompt.":
            "正在開始安裝「Bonjour Service」，請依照 UAC 提示操作。",
        "Installation Failed": "安裝失敗",
        "Failed to install 'Bonjour Service'.\n\nThe application will now exit.":
            "安裝「Bonjour Service」失敗。\n\n應用程式即將結束。",
        "Bonjour Service was installed but could not be started.\n\nOpen Services (services.msc), start 'Bonjour Service', then relaunch uxplay-windows.":
            "Bonjour 服務已安裝但無法啟動。\n\n請開啟服務（services.msc），啟動「Bonjour Service」，然後重新啟動 uxplay-windows。",
        "Bonjour Service Status Unknown": "Bonjour 服務狀態未知",
        "Could not query Bonjour Service status (access denied or service manager unavailable).\n\nDiscovery may still work. Check the log for details.":
            "無法查詢 Bonjour 服務狀態（存取被拒或服務管理員不可用）。\n\n探索仍可能可用，請查看日誌了解詳情。",
        "Restart Failed": "重新啟動失敗",
        "Could not restart uxplay-windows.\n\nPlease close and open it manually.":
            "無法重新啟動 uxplay-windows。\n\n請手動關閉後再開啟。",
        "Initializing...": "初始化中…",
        "AirPlay Name": "AirPlay 名稱",
        "Apply": "套用",
        "Enable Bluetooth Discovery": "啟用藍牙探索",
        "Force Fullscreen (must select renderer)": "強制全螢幕（必須選擇 renderer）",
        "Video Renderer (Auto)": "視訊 Renderer（自動）",
        "Video Renderer": "視訊 Renderer",
        "Video pacing": "視訊節奏",
        "Audio pacing": "音訊節奏",
        "AirPlay timestamps (recommended)": "AirPlay 時間戳（建議）",
        "Arrival (may tear)": "依到達時間（可能撕裂）",
        "Arrival feeds audio as packets arrive. AirPlay timestamps keeps "
        "the original audio PTS for comparison.":
            "音訊依封包到達時間播放；AirPlay 時間戳保留原始音訊 PTS 供比較。",
        "Arrival (stable)": "依到達時間（穩定）",
        "Video is presented as frames arrive and avoids AirPlay PTS "
        "stalls. Experimental timestamp pacing is not exposed because "
        "d3d11videosink sync=true stops after a few frames even with a "
        "local 60 Hz timeline.":
            "影片依實際到達的影格呈現，避免 AirPlay PTS 卡住。"
            "實驗性的時間戳節奏未開放，因為即使本機 60 Hz 時間軸正確，"
            "d3d11videosink sync=true 仍會在幾帧後停止。",
        "OpenGL (test)": "OpenGL（測試）",
        "Direct3D9 (test)": "Direct3D9（測試）",
        "Video Codec": "視訊編碼",
        "HEVC / H.265 (needs >1080p)": "HEVC / H.265（需高於 1080p）",
        "HEVC / H.265 (experimental; may freeze)":
            "HEVC / H.265（實驗性；可能凍結）",
        "H.264 / AVC": "H.264 / AVC",
        "HEVC requires a requested height above 1080p (for example 4K). At 1080p the client uses H.264 even when HEVC is enabled.":
            "HEVC 需要請求高度高於 1080p（例如 4K）。在 1080p 時，即使啟用 HEVC，用戶端仍會使用 H.264。",
        "HEVC requires a requested height above 1080p (for example 4K). At 1080p the client uses H.264 even when HEVC is enabled. HEVC is experimental in this build; use H.264 if the video freezes.":
            "HEVC 需要請求高度高於 1080p（例如 4K）。在 1080p 時，即使啟用 HEVC，用戶端仍會使用 H.264。此版本的 HEVC 為實驗性；若畫面凍結，請改用 H.264。",
        "HEVC is experimental in this build; if the video freezes, switch to H.264.":
            "此版本的 HEVC 為實驗性；若畫面凍結，請改用 H.264。",
        "Resolution": "解析度",
        "Native (device)": "原生（裝置）",
        "Custom": "自訂",
        "Native uses the configured width/height below; set Custom first to change them. It is a request, not an automatic device query.":
            "原生會使用下方的寬高；請先選自訂再修改。這是請求值，不是自動查詢裝置。",
        "OBS output (USB capture)": "OBS 輸出（USB 擷取）",
"OBS output": "OBS 輸出",
        "Off": "關閉",
        "Clean window (Window Capture)": "乾淨視窗（視窗擷取）",
        "Virtual camera (not implemented)": "虛擬攝影機（尚未實作）",
"Virtual camera (use AirPlay option below)": "虛擬攝影機（請用下方 AirPlay 選項）",
"AirPlay virtual camera (experimental)": "AirPlay 虛擬攝影機（實驗性）",
"Publishes the AirPlay video to the SWVCamMediaSource virtual camera. Apply engine changes to start or stop it.":
"將 AirPlay 畫面發布到 SWVCamMediaSource 虛擬攝影機；套用引擎變更後生效。",
        "USB: not checked": "USB：尚未檢查",
        "USB: not started (press Connect)": "USB：尚未啟動（按連線）",
        "Connect USB device": "連線 USB 裝置",
        "Clean window: press Connect USB device, then select the window in OBS.":
            "乾淨視窗：按「連線 USB 裝置」，然後在 OBS 選擇該視窗。",
        "Edit Settings (Advanced)": "編輯設定（進階）",
        "List UxPlay arguments": "列出 UxPlay 參數",
        "License Information": "授權資訊",
        "Open Log": "開啟日誌",
        "Open Log With...": "以其他程式開啟日誌...",
        "Open Log Folder": "開啟日誌資料夾",
        "Open Diagnostics Folder": "開啟診斷資料夾",
        "Open Diagnostics Viewer": "開啟診斷檢視器",
        "Diagnostics Viewer": "診斷檢視器",
        "The viewer could not start. Run this from the project folder:\npython scripts/diagnostic_viewer.py":
            "無法啟動檢視器。請在專案資料夾執行：\npython scripts/diagnostic_viewer.py",
        "Could not launch the diagnostics viewer.": "無法啟動診斷檢視器。",
        "Open the log, choose a viewer, or open log/diagnostics folders.":
            "開啟日誌、選擇檢視器，或開啟日誌／診斷資料夾。",
        "Keep diagnostics": "保留診斷記錄",
        "Keep recent events and metrics in memory for export.":
            "在記憶體保留最近事件與指標，供匯出使用。",
        "Save Diagnostics": "儲存診斷",
        "Diagnostics Window": "診斷視窗",
        "Runtime Debug": "執行時除錯",
        "Open the raw runtime state, counters and recent structured events.":
            "開啟原始執行時狀態、計數器與最近結構化事件。",
        "Copy all": "全部複製",
        "Save...": "儲存...",
        "Save runtime debug": "儲存執行時除錯",
        "Text files (*.txt);;All files (*)": "文字檔 (*.txt);;所有檔案 (*)",
        "Audio Format: ": "音訊格式：",
        "Media Info HUD": "媒體資訊 HUD",
        "USB: ": "USB: ",
        "Video stalled; automatic rebuild is disabled. Press \"Rebuild AirPlay stream\" if it does not recover.":
            "畫面停住；自動重建已停用。若沒有自行恢復，請按「重建 AirPlay 串流」。",
        "Draw the media information in the AirPlay video frame.":
            "在 AirPlay 影片畫面顯示媒體資訊。",
        "HUD is temporarily unavailable while the overlay is replaced.":
            "HUD 暫時無法使用，目前正在更換 overlay 實作。",
        "Waiting for AirPlay client...": "等待 AirPlay 用戶端…",
        "HUD margin": "HUD 邊距",
        "Fields...": "欄位...",
        "Choose which lines the HUD shows.": "選擇 HUD 要顯示的欄位。",
        "Rebuild AirPlay stream": "重建 AirPlay 串流",
        "Stop and restart the engine if the video window is frozen or missing.":
            "影片視窗凍結或消失時，停止並重新啟動 engine。",
        "Show Settings": "顯示設定",
        "Media Info": "媒體資訊",
        "Stop AirPlay": "停止 AirPlay",
        "USB Clean Output (experimental)": "USB 乾淨輸出（實驗性）",
        "Logs & Diagnostics": "日誌與診斷",
        "Keep recent diagnostics": "保留近期診斷",
        "Video always on top": "影片永遠置頂",
        "HUD position": "HUD 位置",
        "Top left": "左上",
        "Top right": "右上",
        "Bottom left": "左下",
        "Bottom right": "右下",
        "HUD fields": "HUD 欄位",
        "Restart": "重新啟動",
        "Quit": "結束",
        "AirPlay stopped": "AirPlay 已停止",
        "Engine failing - see log": "Engine 發生錯誤 — 請看日誌",
        "UxPlay server starting...": "UxPlay 伺服器啟動中…",
        "Streaming: %1": "串流中：%1",
        "Connected: %1": "已連線：%1",
        "UxPlay server running - waiting for connection":
            "UxPlay 伺服器執行中 — 等待連線",
        "Open uxplay-windows on login: ON": "登入時開啟 uxplay-windows：開",
        "Open uxplay-windows on login: OFF": "登入時開啟 uxplay-windows：關",
        "Start AirPlay": "開始 AirPlay",
        "Language": "語言",
        "System language": "跟隨系統",
        "English": "英文",
        "Traditional Chinese": "繁體中文",
        "Restart uxplay-windows to apply the language change.":
            "請重新啟動 uxplay-windows 以套用語言變更。",
    },
    "DiagnosticsWindow": {
        "Diagnostics - uxplay-windows": "診斷 - uxplay-windows",
        "uxplay-windows diagnostics": "uxplay-windows 診斷",
        "Overview": "總覽",
        "Video": "視訊",
        "Audio": "音訊",
        "Network": "網路",
        "Events": "事件",
        "Copy summary": "複製摘要",
        "Copy all": "全部複製",
        "Save diagnostics": "儲存診斷",
        "Close": "關閉",
        "No recorded events yet.": "尚無已記錄事件。",
        "Time": "時間",
        "Status": "狀態",
        "Client": "用戶端",
        "Device ID": "裝置 ID",
        "Connections": "連線數",
        "Session uptime": "工作階段執行時間",
        "App version": "應用程式版本",
        "OBS mode": "OBS 模式",
        "USB": "USB",
        "Last error": "最後錯誤",
        "Settings": "設定",
        "Log": "日誌",
        "Diagnostics": "診斷",
        "Engine arguments": "Engine 參數",
        "Codec": "編碼",
        "Output": "輸出",
        "Requested": "請求",
        "Decoder": "解碼器",
        "Renderer": "Renderer",
        "FPS": "FPS",
        "Frame interval": "影格間隔",
        "Last frame age": "最後影格年齡",
        "Presented frames": "已呈現影格",
        "Input buffers": "輸入緩衝",
        "Input age": "輸入年齡",
        "Format": "格式",
        "Reported latency": "回報延遲",
        "Packets received": "已接收封包",
        "Packets delivered": "已交付封包",
        "Resend requests": "重送請求",
        "Empty resend responses": "空重送回應",
        "Bytes received": "已接收位元組",
        "A rising empty-resend count means the client asked for audio that it no longer had available; this can sound like a short audio drop.":
            "空重送回應持續增加，代表用戶端要求它已不再保留的音訊；聽起來可能是一段短暫的音訊中斷。",
        "RTP and connection details are recorded in the diagnostics bundle. Use Save diagnostics when the issue reproduces.":
            "RTP 與連線細節會記錄在診斷包中。問題重現時請使用「儲存診斷」。",
    },
    "main": {
        "uxplay-windows is already running.\nCheck the system tray.":
            "uxplay-windows 已在執行。\n請查看系統托盤。",
        "Another uxplay-windows instance is still running.":
            "仍有另一個 uxplay-windows 實例在執行。",
        "Error": "錯誤",
        "System tray not available.": "系統托盤不可用。",
    },
    "HudWindow": {
        "Media Info HUD": "媒體資訊 HUD",
    },
    "RuntimeDebugWindow": {
        "Runtime Debug": "執行時除錯",
        "Copy all": "全部複製",
        "Copied": "已複製",
        "Copied %1 characters": "已複製 %1 個字元",
        "Save...": "儲存...",
        "Save runtime debug": "儲存執行時除錯",
        "Text files (*.txt);;All files (*)": "文字檔 (*.txt);;所有檔案 (*)",
    },
}

translations["MainWindow"].update({
    "HUD display": "HUD 顯示方式",
    "Separate window": "獨立視窗",
    "Video overlay (legacy)": "影片疊層（舊實作）",
    "Show media information using the selected HUD display mode.": "使用選定的 HUD 顯示方式顯示媒體資訊。",
    "The legacy overlay follows the video window. If video presentation stalls, switch back to the separate window.": "舊疊層會跟隨影片視窗。若畫面呈現停滯，請切回獨立視窗。",
})

path = Path(__file__).resolve().parents[1] / "translations/uxplay-windows_zh_TW.ts"
tree = ET.parse(path)
root = tree.getroot()
missing = []
for context in root.findall("context"):
    name = context.findtext("name")
    if not name:
        continue
    for message in context.findall("message"):
        source = message.findtext("source")
        if source is None:
            continue
        translation = message.find("translation")
        value = translations.get(name, {}).get(source)
        if value is None or translation is None:
            missing.append(f"{name}: {source!r}")
            continue
        translation.attrib.pop("type", None)
        translation.text = value
ET.indent(tree, space="    ")
tree.write(path, encoding="utf-8", xml_declaration=True)
if missing:
    print("Missing translations:")
    print("\n".join(missing))
    raise SystemExit(1)
print("Updated", path)

# USB capture spike

## Goal

Prove how an iPad/iPhone screen and audio can be captured over USB on
Windows, before wiring anything into the app.

This is **not AirPlay**. The library is `usbcapture/`; the AirPlay engine
(`libuxplay`) is untouched.

## Verdict (2026-11-11)

Two Windows-capable paths were evaluated on an iPad13,18 (iOS 27.3):

| Path | Driver | Developer Mode | iOS | Status |
| --- | --- | --- | --- | --- |
| QuickTime USB | kernel filter (libusb0) | no | broad | blocked in user mode |
| CoreDevice display | none | yes + DDI | 27+ | **verified** |

- **QuickTime USB** needs a kernel-mode filter driver on Windows. A normal
  process cannot open the Apple mux interface or switch the device to its
  QuickTime configuration.
- **CoreDevice display** streams live HEVC video and AAC-ELD audio through
  the no-root userspace RSD tunnel. It needs Developer Mode and a mounted
  DDI, but no driver. Video and audio were captured and decoded on this
  machine.

## Device descriptors

PID `05ac:12ab` exposes four USB configurations:

| Config | Interfaces |
| --- | --- |
| 1 | PTP |
| 2 | audio + HID |
| 3 | PTP + Apple mux `ff/fe/02` |
| 4 | PTP + Apple mux + `ff/fd/01` (QT) |

- Active config is 3. Windows creates two children: `MI_00` (PTP, WPD)
  and `MI_01` (Apple mux, `WINUSB` behind Apple's UMDF filter).
- The QuickTime interface is config 4, interface 2: class `ff`, subclass
  `fd`, protocol `01`, alternates 0/1/2, bulk IN `0x86` and OUT `0x05`.
- `quicktime_video_hack` hardcodes `QuicktimeSubclass = 0x2A`, so it finds
  zero devices on iOS 27.3. The subclass on this device is `0xFD`.

## Why QuickTime USB fails in user mode

- `libusb_open` returns `LIBUSB_ERROR_IO` on the composite device.
- Opening Apple's WinUSB mux interface (`{664be590-...}`) fails with
  `ERROR_NOT_SUPPORTED` (50) for read, write, none and overlapped access.
- Apple's `oem55.inf` binds WinUSB to `MI_01` only, behind
  `WUDFRd`, `AppleKmdfFilter` and the UMDF `AppleUsbFilter`; there is no
  binding for the QT interface.
- The known working design is a per-device **libusb0 upper filter** on the
  Apple `usbccgp` device, because only libusb-win32 supports
  `usb_set_configuration` on Windows (see iPhoneMirror and qvh issue 122).
  Do **not** replace the parent driver with Zadig; that breaks Apple's
  service.
- That path needs a signed kernel driver and an installer. It is not
  implemented in this spike.

## CoreDevice display stream (verified)

Requirements: iOS 27+, Developer Mode, Developer Disk Image mounted.
The RSD tunnel is a pure-Python userspace stack, so no admin is needed.

```powershell
py -3.11 -m pymobiledevice3 amfi developer-mode-status
py -3.11 -m pymobiledevice3 mounter auto-mount
py -3.11 -m pymobiledevice3 developer core-device display `
    get-media-support-info
py -3.11 -m pymobiledevice3 developer core-device display `
    start-video-stream out\ipad.rtp --duration 5
py -3.11 tools\rtp_dump.py out\ipad.rtp out\ipad.h265
ffprobe -hide_banner out\ipad.h265
ffmpeg -framerate 60 -i out\ipad.h265 -frames:v 1 out\ipad-frame.png
py -3.11 -m pymobiledevice3 developer core-device display `
    start-audio-stream out\ipad-audio.rtp --duration 5
```

Measured result:

- 5 s video: 1646 RTP packets, 1.8 MB, HEVC Main 1648x2384.
- 5 s audio: 504 packets, payload type 101, AAC-ELD (mode 8).
- The extracted frame shows the iPad screen; `ffmpeg` and GStreamer
  `avdec_h265` both decode the stream.

### Live viewer

```powershell
py -3.11 -m pymobiledevice3 developer core-device display `
    serve-web --http-port 8099
```

Open `http://127.0.0.1:8099/` in Edge or Chrome. The page decodes HEVC
with WebCodecs; this machine reported `hev1.1.6.L150.B0` and held 60-67
fps. The viewer also exposes rotate, zoom, volume, Home/Siri/Swipe,
clipboard and accessibility controls.

The raw stream follows the device orientation (`landscapeLeft` on this
iPad), so the viewer needs its rotate control; a native integration must
apply the same transform. Older browsers without HEVC WebCodecs support
cannot decode this page.

### Native renderer bridge

`tools/stream_to_tcp.py` reads the sidecar's `/stream.bin` and rewrites the
length-prefixed HEVC access units as Annex-B to a local TCP socket:

```powershell
py -3.11 tools\stream_to_tcp.py --http-port 8099 --tcp-port 5005
gst-launch-1.0 tcpclientsrc host=127.0.0.1 port=5005 ! `
    h265parse ! avdec_h265 ! videoflip method=counterclockwise ! `
    videocrop right=8 bottom=24 ! videoconvert ! d3d11videosink
```

The bridge prepends VPS/SPS/PPS from the `/codec` hvcC record on keyframes.
Verified on 2026-11-12:

- 240+ access units reached GStreamer; the live `d3d11videosink` window
  played without errors.
- Raw frames are 1648x2384; `/codec` reports 1640x2360 with
  `rotation: -90`.
- `videoflip method=counterclockwise` makes the UI upright; this is the
  same orientation the web viewer applies.
- `videocrop right=8 bottom=24` removes the encoder padding and restores
  the 1640x2360 display aspect ratio.

The bridge itself does not rotate; the renderer owns rotation. The clean
OBS output must not draw the debug viewer's device frame and must not use
the viewer's stretch-to-fill canvas lock.

### Clean Output in the app

The app runs the same pipeline through `UsbObsController` and
`CleanOutputWindow`:

```powershell
build\dev\run-dev.cmd --usb-obs
```

Or use the tray menu / main-window button `USB Clean Output`.
It opens a frameless window titled `uxplay-windows USB Clean Output`
with no device bezel, no browser chrome and no controls. In OBS choose
**Window Capture** and select that title; the right-click menu can copy
the source name.

The controller starts the Python sidecar, the bridge and GStreamer, and
applies `display.rotation` and the hvcC crop itself. One capture session
feeds only the clean window.

The persisted mode lives in `settings.json`:

```json
"obs": { "mode": "off" }
```

- `off` (default): USB capture is not started.
- `window`: start the USB Clean Output; OBS uses Window Capture.
- `camera`: reserved for the virtual camera, not implemented.

`--usb-obs` selects `window` and persists it. Closing the clean window
switches the mode back to `off` and stops the sidecar/bridge processes.

The OBS group also has a USB status line and `Connect USB device`
button. It runs the preflight (usbmux list -> lockdown pair if needed ->
Developer Mode -> mounter auto-mount) and only starts the sidecar when
ready. `--usb-obs` uses the same preflight.

The window can be dragged anywhere; resize it from its 8 px edges.
Right-click offers `Move window`, `Resize window`, `Always on top`,
`Copy OBS source name`, `Reset size` and `Close`.

AirPlay still renders into the engine's native video window; only the USB
path uses this `CleanOutputWindow`. A shared AirPlay clean output needs an
engine-side window-handle hook (see `TODO.md`).

Prerequisites: Python 3 with `pymobiledevice3`, Developer Mode, a mounted
DDI, and a trusted device. `UXPLAY_USB_PYTHON` overrides the interpreter
(e.g. `py -3.11`). Video only; AAC-ELD audio is not decoded on Windows yet.

Limitations:

- Developer Mode and a mounted DDI are hard requirements. This is not a
  normal consumer flow.
- The CLI documents video streaming as iOS 27+.
- DRM-protected video may render black, as with any screen capture.
- Product integration currently uses a Python sidecar; a native C++ port
  is still future work.

## Tools in this directory

- `check_env.ps1`: pairing and device checks.
- `capture.ps1`: QuickTime record path (not usable on Windows yet).
- `tools/enumerate_usb.py`: descriptor dump through pyusb (read-only).
- `tools/qt_winusb.py`: WinUSB access probe; documents the Apple filter
  rejection.
- `tools/rtp_dump.py`: RTP/HEVC unwrap to Annex-B `.h265`.
- `tools/stream_to_tcp.py`: `/stream.bin` to Annex-B bridge for GStreamer.
- `patches/quicktime_video_hack-windows.patch`: GStreamer removal and
  CGO build fix for the upstream tool.

## QuickTime reference (Linux/macOS)

```bash
cd tools
git clone https://github.com/danielpaulus/quicktime_video_hack
cd quicktime_video_hack
go build -o quicktime_video_hack .
./quicktime_video_hack record out.h264 out.wav --udid <udid>
```

On iOS 27 the QT subclass must be detected as `0xFD`, not only `0x2A`.

## Acceptance

1. Device is paired and trusted.
2. A 10 minute stream runs without stalling.
3. Video and audio both arrive, or the limitation is documented.
4. Unplug/replug recovers without restarting the app.

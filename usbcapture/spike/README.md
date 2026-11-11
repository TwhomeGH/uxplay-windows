# QuickTime USB capture spike

## Goal

Prove on this Windows machine that an iPad/iPhone can be captured over USB
through the QuickTime service, before wiring anything into the app.

This is **not AirPlay**. The library is `usbcapture/`; the AirPlay engine
(`libuxplay`) is untouched.

## Current state (2026-11-11)

- `usbcapture` builds with `-DENABLE_USB_CAPTURE=ON`.
- `libimobiledevice` 1.3.0 and `libusbmuxd` 2.1.1 are installed in UCRT64.
- Apple Mobile Device Service is running.
- The iPad appears as Apple Mobile Device USB Composite Device, but
  `idevice_id -l` is empty: unlock the device and tap **Trust This
  Computer**, then retry.
- `quicktime_video_hack` does not build on Windows: its `gstadapter`
  imports `github.com/danielpaulus/gst`, which is CGO/Linux-only and fails
  with `build constraints exclude all Go files`. Use `pymobiledevice3` or
  a Linux/macOS host for the spike.

## Check

```powershell
.\check_env.ps1
```

## Pair and list

```powershell
H:\FIX_VIAP\msys64\ucrt64\bin\idevice_id.exe -l
H:\FIX_VIAP\msys64\ucrt64\bin\idevicepair.exe pair
H:\FIX_VIAP\msys64\ucrt64\bin\idevicepair.exe validate
H:\FIX_VIAP\msys64\ucrt64\bin\ideviceinfo.exe -k ProductVersion
```

## Capture

`capture.ps1` expects `tools/quicktime_video_hack/quicktime_video_hack.exe`.
On Windows the upstream Go tool currently does not build; on Linux/macOS:

```bash
cd tools
git clone https://github.com/danielpaulus/quicktime_video_hack
cd quicktime_video_hack
go build -o quicktime_video_hack .
./quicktime_video_hack -udid <udid> -o out.h264
```

Then inspect:

```bash
ffprobe -hide_banner out.h264
ffplay out.h264
gst-launch-1.0 filesrc location=out.h264 ! h264parse ! avdec_h264 ! d3d12videosink
```

Alternative: `pymobiledevice3` for device services and screenshots; confirm
whether the current version exposes a live QuickTime stream before
committing to it.

## Acceptance

1. `idevice_id -l` lists the device.
2. `idevicepair validate` returns paired.
3. H.264 frames arrive for 10 minutes.
4. Audio is present or the limitation is documented.
5. Unplug/replug recovers without restarting the app.

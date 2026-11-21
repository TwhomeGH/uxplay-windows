# Virtual camera spike

This directory holds the first working Windows 11 virtual-camera spike for
UxPlay Windows. It is intentionally separate from the AirPlay engine and from
`libuxplay`; the virtual camera is an output path, not a replacement for the
video renderer.

## Current status

Verified on Windows 11 build 26200 with the official Microsoft
`Windows-Camera` VirtualCamera sample:

- media source DLL built with Visual Studio 2022 Community
- COM source registered under HKLM
- virtual camera created as `SWVCamMediaSource`
- DirectShow exposes it as `SWVCamMediaSource (Windows 虛擬攝影機)`
- a Python test producer published 2360x1640 NV12 at 60 fps through the pipe
- ffmpeg captured the virtual camera and two extracted frames had different
  SHA-256 hashes, proving the output was live rather than a synthetic still

## Architecture

```text
AirPlay request (native 2360x1640)
  -> videoscale (no low-resolution downscale)
  -> NV12 2360x1640 @ source rate (up to 60 fps)
  -> appsink
  -> named pipe
  -> SWVCamMediaSource
  -> OBS / Camera / Zoom
```

The media source creates the pipe because it runs in the Windows Frame Server.
It keeps only the latest complete frame; consumers never block the AirPlay
video pipeline. A named pipe is used instead of a session-local shared mapping
because the Frame Server is outside the interactive user's `Local\` namespace.

The producer and media source use the frame header in
[`protocol/SharedFrame.h`](protocol/SharedFrame.h):

- `magic` = `UXVC`
- `version` = 1
- `width`, `height`, `stride`
- `format`: `1` = RGB32, `2` = NV12
- `data_size`
- `frame_id`, `timestamp_ms`

One pipe message is `FrameHeader` followed immediately by the pixel payload.

The media source is now wired into the AirPlay GStreamer renderer as an
optional `appsink` branch behind `-vcam`. The GUI option is **AirPlay virtual
camera (experimental)**; like the other engine-affecting settings it is saved
immediately but requires **Apply & restart engine**. The branch is:

```text
tee name=vcam_tee
  vcam_tee. ! queue ! d3d11videosink
  vcam_tee. ! queue ! videoconvert ! videoscale add-borders=true
             ! video/x-raw,format=NV12,width=2360,height=1640
             ! appsink name=vcam_sink
```

The writer keeps only the latest frame and drops frames when no camera
consumer is connected, so it cannot block the AirPlay pipeline.

## Media source files

[`media-source/SimpleFrameGenerator.*`](media-source) are an overlay for the
official sample's
`Samples/VirtualCamera/VirtualCameraMediaSource` project. They keep the
sample's synthetic fallback when no producer is connected, and otherwise copy
the latest pipe frame into the Media Foundation sample buffer.

## Build the official sample

Prerequisites:

- Windows 11
- Visual Studio 2022 with C++ desktop tools
- Windows SDK 10.0.22000 or newer
- NuGet

```powershell
git clone --depth 1 https://github.com/microsoft/Windows-Camera.git build\Windows-Camera
```

Restore the sample's NuGet packages, then build the media source:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_virtualcam.ps1 -Installer
```

The script clones the official sample when needed, restores NuGet packages,
applies the frame-pipe overlay, and builds both the media source and the
sample installer.

## Register and create the virtual camera

The media source DLL must be registered in HKLM (administrator):

```text
CLSID {7B89B92E-FE71-42D0-8A41-E137D06EA184}
InProcServer32 = C:\ProgramData\uxplay-vcam\VirtualCameraMediaSource.dll
ThreadingModel = Both
```

Then run the sample installer as administrator and choose:

```text
1 - register
1 - VCam-SimpleMediaSource
1 - System lifetime
2 - All users
```

Use **All users** for a camera that ordinary DirectShow applications can see.
With `Current User`, the camera was created but did not appear in the ffmpeg
DirectShow device list on the test machine.

## Publish test frames

Start the test producer before opening the camera:

```powershell
python virtualcam\tools\shared_frame_writer.py --seconds 30
```

Capture the virtual camera:

```powershell
ffmpeg -f dshow -i "video=SWVCamMediaSource (Windows 虛擬攝影機)" `
  -t 5 -c:v libx264 -preset ultrafast -pix_fmt yuv420p build\vcam-v2.mp4
```

## Limitations and next steps

- The producer is currently a test pattern, not the AirPlay decoder.
- The AirPlay `appsink` branch is implemented and the pipeline parses, but the
  end-to-end AirPlay-to-virtual-camera path still needs a reconnect test with
  the iPad.
- the pipe ACL is `Everyone` for the spike; production must restrict it to the
  interactive user and the Frame Server services.
- The current native output is the test iPad's 2360x1640 at 60 fps; a per-device
  native-resolution config is the next step.
- The virtual camera is video-only; audio is handled separately.
- HUD text is not included in the virtual camera.
- The media source still needs to be packaged and registered by the UxPlay
  installer instead of the sample installer.
- The AirPlay `keyframes=1` / decoder-only-first-frame defect is independent
  and must be fixed separately.

## Next integration step

Add a GStreamer `appsink` branch to the AirPlay video pipeline, convert its
frames to NV12, and write each frame to the pipe from the Qt application. The
existing `d3d11videosink` path stays in place so the local preview and OBS
output do not depend on each other.

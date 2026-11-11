# usbcapture

QuickTime USB capture backend for uxplay-windows. **Not AirPlay**, and not
part of `libuxplay`.

## Status

Skeleton only. `isBackendAvailable()` returns false and no packets are
captured yet. The QuickTime protocol spike is tracked in `TODO.md`.

## Why a separate library

`libuxplay` is a git submodule implementing AirPlay over IP (mDNS, RAOP,
FairPlay). A USB cable does not carry AirPlay. The USB mirroring mode used
by Reflector/AirServer is Apple's QuickTime capture service exposed over
usbmuxd, so it has a different transport, discovery, pairing and failure
model.

Keeping it in `usbcapture/` means:

- `libuxplay` stays AirPlay-only.
- `src/` only holds the Qt adapter (`UsbCaptureWorker`).
- The feature stays behind a CMake option and can be omitted from
  AirPlay-only builds.

## Architecture

```text
iPhone --USB--> usbmuxd / Apple Mobile Device Service
             -> libimobiledevice (pairing, trust, lockdown)
             -> QuickTime capture service
             -> H.264 video + audio
             -> GStreamer appsrc
             -> renderer
```

## Dependencies

- Windows: Apple Mobile Device Support (installs
  `AppleMobileDeviceService`, the usbmuxd implementation) and the Apple
  device driver.
- `libimobiledevice` and `libusbmuxd` (MSYS2 UCRT64 packages).
- GStreamer 1.24+ (same stack as the AirPlay renderer).
- The QuickTime capture service is private/undocumented; this is why the
  spike comes before integration.

## Build

Default is off; the AirPlay build does not require any USB dependency.

```bash
cmake -S . -B build/usb -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_USB_CAPTURE=ON
cmake --build build/usb
```

If `libimobiledevice`/`libusbmuxd` are missing, configure fails with the
pkg-config error; that is intentional for the opt-in build.

## Layout

- `include/usbcapture/`: public API, no Qt and no `libuxplay` headers.
- `src/`: usbmuxd client, QuickTime session, demux and GStreamer pipeline.
- `tests/`: hardware and sample-based test plan.
- `../src/usbworker.*`: Qt adapter, added when integration starts.

## Test plan

- Unit: demux/framing with captured samples, no device.
- Hardware: iPhone 15 / older USB-C, iOS 17/18/26, USB 2 and 3 ports.
- Long-run: 30+ minutes, screen lock, rotation, audio, reconnect.
- DRM: Netflix/Apple TV (expected black frame; document it).
- Failure: unplug mid-stream, trust denied, Apple service stopped.

## Non-goals

- AirPlay discovery/mDNS/PIN; this library never registers a Bonjour
  service.
- Android USB/ADB; can be a separate backend later.
- USB-C DisplayPort capture; that is a hardware/UVC path, not this
  protocol.

## References

- `quicktime_video_hack` (protocol reference/PoC)
- `pymobiledevice3` (device services, developer tooling)
- `libimobiledevice` (pairing/lockdown/AFC)

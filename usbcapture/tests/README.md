# usbcapture tests

Planned coverage; no test targets yet.

## Unit

- QuickTime framing/demux from captured samples, no iPhone required.
- H.264 access-unit boundary handling.
- Audio format detection once the spike documents it.
- Error paths: trust denied, service missing, unplug mid-stream.

## Hardware

- iPhone 15 (USB-C) and an older USB-C iPhone.
- iOS 17, 18 and current (2026) on both devices.
- USB 2 and USB 3 ports, direct cable only (no hub) first.
- 30+ minute runs, screen lock/unlock, rotation, audio, reconnect.
- DRM content: expect black frames; record the exact behavior.

## Acceptance for the spike

1. `usbmuxd`/Apple Mobile Device Service sees the device.
2. Pairing/trust succeeds without iTunes UI.
3. H.264 frames arrive continuously for 10 minutes.
4. Audio arrives and stays in sync within the documented tolerance.
5. Unplug/replug recovers without restarting the app.

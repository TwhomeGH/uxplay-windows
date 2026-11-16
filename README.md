# FREE AirPlay to your Windows PC

Free as both in "freedom" and "free beer"!

This repository is a **maintained fork** of
[leapbtw/uxplay-windows](https://github.com/leapbtw/uxplay-windows), built on
[UxPlay](https://github.com/FDH2/UxPlay/). It adds lifecycle controls,
diagnostics, localization, HUD telemetry and a small set of Windows-focused
fixes on top of the original project.

## Download

Nightly builds are published as prereleases with unique tags:

- **x64:** [nightly-x64 releases](https://github.com/TwhomeGH/uxplay-windows/releases?q=nightly-x64)
- **arm64:** [nightly-arm64 releases](https://github.com/TwhomeGH/uxplay-windows/releases?q=nightly-arm64)

The publisher keeps the ten most recent releases per architecture and removes
older ones. The historical `nightly-build-x64` / `nightly-build-arm64` tags
are no longer updated.

The x64 release contains:

```text
uxplay-windows-x64.zip
uxplaywindows-installer.msi
```

The arm64 release contains the corresponding arm64 assets.

Each build gets a new tag. Nightlies are not signed release artifacts; use
them for testing and report the commit shown in the release body when something
breaks.

## Features

- AirPlay mirroring and audio playback to Windows 10/11.
- H.264 and HEVC advertisement; the final codec is chosen by the Apple client.
- Native, 720p, 1080p, 1440p, 4K and custom resolution requests.
- Frameless/native video window with remembered geometry, optional always-on-top.
- Media Info HUD with selectable fields, corner position and pixel margins.
- Tray controls: Start/Stop AirPlay, Restart, Rebuild AirPlay stream, Show Settings.
- In-process engine restart, so changing Start/Stop does not restart the whole app.
- Single-instance protection with a second launch raising the existing window.
- Recent diagnostics ring buffer, folder/ZIP export and a local viewer.
- System, English and Traditional Chinese UI.
- Optional USB capture path for OBS, clearly marked experimental and off by default.

## Installation

### MSI installer (recommended)

Download `uxplaywindows-installer.msi` from the latest nightly release and run
it. The installed application is controlled from its tray icon.

### Portable ZIP

Download the ZIP, extract it, and run `uxplay-windows.exe`. Keep the extracted
folder intact: Qt and GStreamer DLLs are next to the executable.

### After installing

- Right-click the tray icon to start, stop or restart AirPlay.
- `Show Settings` opens the configuration window.
- The app may be set to start when you log in.
- If Windows Firewall prompts, allow the application. The MSI installs one
  managed firewall rule for the installed executable; development builds in
  different paths may create separate rules.

> [!IMPORTANT]
> **Why does Windows Defender complain?**
>
> The executable is not code-signed. Click `More info` and continue only if you
> trust the build, or build it yourself. Windows SmartScreen may show the same
> warning.

## Requirements

- Windows 10 or 11 (x64 or arm64).
- Bonjour Service for AirPlay discovery (mDNS). The app checks it and offers
  installation/start guidance when it is missing.
- The Apple device and the PC must be able to reach each other on the same
  network. Guest Wi-Fi, client isolation and VPNs can prevent discovery.

## Usage

1. Start `uxplay-windows`; the tray icon shows the current state.
2. On the Apple device, open Control Center or the AirPlay menu and select the
   receiver name shown in the app.
3. Use the tray menu or main window to adjust resolution, codec, renderer,
   HUD and diagnostics.
4. If the video window freezes, try `Rebuild AirPlay stream` before restarting
   the whole app.

Some renderer, codec and capture settings are applied at the next engine
start. Engine-affecting changes show a pending row; `Apply & restart engine`
restarts only the AirPlay worker, not the whole app.

## Diagnostics

`Save Diagnostics` exports a bounded incident window as a local folder (ZIP is
optional). Bundles contain events, metrics, filtered logs, redacted settings
and system information; nothing is uploaded automatically.

For development, inspect a bundle or the current log with:

```powershell
python scripts/diagnostic_viewer.py
```

The viewer binds to `127.0.0.1` and is read-only.

## Known limitations

- HEVC is only advertised usefully when the requested height is above 1080p;
  at 1080p the Apple client commonly chooses H.264. This is client behavior,
  not a local decoder setting.
- The AirPlay video window is created by the GStreamer renderer; it is not a
  regular Qt window. Window geometry and HUD are managed by the app around it.
- USB capture is opt-in and experimental. It requires Python,
  `pymobiledevice3`, Developer Mode and a mounted Developer Disk Image. Video
  works, but Windows currently lacks the AAC-ELD audio decode path.
- USB data uses the iPad's only USB-C port. On USB-A-only PCs the available
  power is usually far below the iPad's draw; use a PD dock or AirPlay instead.
- Nightly builds are prereleases; only the ten most recent releases per
  architecture are kept, so older download links expire.

## Development

| Document | Contents |
| --- | --- |
| [Developer guide](docs/DEVELOPERS-GUIDE.md) | Build, run, debug, diagnostics, i18n and CI |
| [API guide](docs/API-GUIDE.md) | Source map and Doxygen entry point |
| [Translation rules](translations/README.md) | Keys, placeholders, fallback and checks |
| [Change history](docs/ChangeHistory.md) | Fixes and design decisions |
| [TODO](TODO.md) | Remaining work and priorities |

Quick build on Windows with MSYS2 UCRT64:

```bash
bash scripts/dev_build.sh
bash scripts/dev_run.sh --engine-debug
```

API reference:

```powershell
pwsh -NoProfile -File scripts/build_docs.ps1 -Open
```

## Reporting issues

Use the repository issue form and attach a diagnostics bundle from
`Save Diagnostics`. Include the app version, Windows version, Apple device
model, whether audio and video were both affected, and whether the problem
survives `Rebuild AirPlay stream`.

## License

See [docs/LICENSE.rtf](docs/LICENSE.rtf). This fork retains the upstream
project's licensing and attribution.

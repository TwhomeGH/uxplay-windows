#!/usr/bin/env bash
# Launch a locally built uxplay-windows with the MSYS2 runtime environment.
# Run from an MSYS2 UCRT64 shell; see docs/DEVELOPERS-GUIDE.md.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/dev}"
EXE="$BUILD_DIR/uxplay-windows.exe"
ENGINE_DEBUG=0
DETACH=0

usage() {
    cat <<'EOF'
Usage: scripts/dev_run.sh [options]

Options:
  --engine-debug  Pass -d to the UxPlay engine (debug logging)
  --detach        Start in the background and return
  -h, --help      Show this help

Environment:
  BUILD_DIR        Build directory (default: <repo>/build/dev)
  MSYSTEM_PREFIX   MSYS2 prefix (default: /ucrt64 in UCRT64)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine-debug) ENGINE_DEBUG=1 ;;
        --detach) DETACH=1 ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    echo "ERROR: run this from an MSYS2 UCRT64 shell (MSYSTEM=UCRT64)." >&2
    exit 1
fi

if [[ ! -f "$EXE" ]]; then
    echo "ERROR: $EXE not found. Run 'bash scripts/dev_build.sh' first." >&2
    exit 1
fi

# Stop the previous dev instance before launching the new build. Restrict
# this to the exact executable path so an installed uxplay-windows is not
# killed when running a build-tree binary.
EXE_WIN="$(cygpath -w "$EXE" 2>/dev/null || printf '%s' "$EXE")"
powershell.exe -NoProfile -Command "Get-Process -Name uxplay-windows -ErrorAction SilentlyContinue | Where-Object { \$_.Path -eq '$EXE_WIN' } | Stop-Process -Force; Start-Sleep -Milliseconds 300" >/dev/null 2>&1 || true

PREFIX="${MSYSTEM_PREFIX:-/ucrt64}"
export PATH="$PREFIX/bin:$PATH"
export QT_PLUGIN_PATH="$PREFIX/share/qt6/plugins"

# Release packages bundle plugins under <exe dir>/lib/gstreamer-1.0.
# A dev build does not, so use the MSYS2 plugin directory instead.
if [[ ! -d "$BUILD_DIR/lib/gstreamer-1.0" ]]; then
    export GST_PLUGIN_PATH="$PREFIX/lib/gstreamer-1.0"
fi

# Keep the tray icon available for dev runs.
mkdir -p "$BUILD_DIR/resources"
if [[ ! -f "$BUILD_DIR/resources/icon.ico" && -f "$REPO_ROOT/stuff/newicon.ico" ]]; then
    cp -f "$REPO_ROOT/stuff/newicon.ico" "$BUILD_DIR/resources/icon.ico"
fi

ARGS=()
if [[ "$ENGINE_DEBUG" -eq 1 ]]; then
    ARGS+=("--engine-debug")
fi

if [[ -n "${LOCALAPPDATA:-}" ]]; then
    echo "Log: $LOCALAPPDATA\\leapbtw\\uxplay-windows\\logs\\uxplay-windows.log"
else
    echo "Log: %LOCALAPPDATA%\\leapbtw\\uxplay-windows\\logs\\uxplay-windows.log"
fi

if [[ "$DETACH" -eq 1 ]]; then
    ( "$EXE" "${ARGS[@]}" & )
    echo "Started detached: $EXE"
    exit 0
fi

exec "$EXE" "${ARGS[@]}"

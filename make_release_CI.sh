#!/bin/bash
set -e

EXE_NAME="uxplay-windows.exe"
BEACON_EXE="uxplay-bluetooth-beacon.exe"
DIST_DIR="$(pwd)/release"
BUILD_DIR="$(pwd)/build"
BEACON_DIR="$(pwd)/libuxplay/Bluetooth_LE_beacon"

PROJECT_ROOT="$(pwd)"
BONJOUR_SDK_HOME="$PROJECT_ROOT/Bonjour SDK"

export BONJOUR_SDK_HOME
export BONJOUR_SDK="$BONJOUR_SDK_HOME"

if [ ! -f "$BONJOUR_SDK_HOME/Include/dns_sd.h" ]; then
  echo "ERROR: dns_sd.h not found at: $BONJOUR_SDK_HOME/Include/dns_sd.h" >&2
  exit 1
fi
if [ ! -f "$BONJOUR_SDK_HOME/Lib/x64/dnssd.lib" ]; then
  echo "ERROR: dnssd.lib not found at: $BONJOUR_SDK_HOME/Lib/x64/dnssd.lib" >&2
  exit 1
fi

echo "================================================="
echo " 1. Compiling C++ App (CMake + Ninja)"
echo "================================================="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $PROJECT_ROOT -DNO_MARCH_NATIVE=ON ..
ninja
cd ..

echo "================================================="
echo " 2. Compiling Python BLE Beacon (PyInstaller)"
echo "================================================="
cd "$BEACON_DIR"

pyinstaller \
  --onefile \
  --name ${BEACON_EXE%.*} \
  --hidden-import=winrt.windows.foundation \
  --hidden-import=winrt.windows.foundation.collections \
  --hidden-import=winrt.windows.devices.bluetooth.advertisement \
  --hidden-import=winrt.windows.storage.streams \
  --hidden-import=psutil \
  --console \
  uxplay-beacon-windows.py \
  --noconfirm

cd ../..

echo "================================================="
echo " 3. Preparing Clean Dist Folder"
echo "================================================="
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/lib/gstreamer-1.0"
[ -f "docs/LICENSE.rtf" ] && cp "docs/LICENSE.rtf" "$DIST_DIR/"

cp "$BUILD_DIR/$EXE_NAME" "$DIST_DIR/"
cp "$BEACON_DIR/dist/$BEACON_EXE" "$DIST_DIR/"

echo "================================================="
echo " 4. Gathering BIG runtime bundle for CI (only from stuff/dll.txt)"
echo "================================================="

DLL_LIST_FILE="$PROJECT_ROOT/stuff/dll.txt"
if [ ! -f "$DLL_LIST_FILE" ]; then
  echo "ERROR: dll.txt not found at: $DLL_LIST_FILE" >&2
  exit 1
fi

copy_one_dll() {
  local win_path="$1"
  [ -z "$win_path" ] && return 0

  local unix_path="$win_path"
  if [[ "$win_path" =~ ^[A-Za-z]:\\ ]]; then
    unix_path="$(cygpath -u "$win_path")"
  fi

  local prefix="${MSYSTEM_PREFIX:-/ucrt64}"
  unix_path="$(printf '%s' "$unix_path" | sed "s#/ucrt64/#$prefix/#g")"

  case "$unix_path" in
    *.dll|*.DLL) ;;
    *) return 0 ;;
  esac

  if [ ! -f "$unix_path" ]; then
    echo "WARNING: listed DLL not found, skipped: $unix_path" >&2
    return 0
  fi

  local dest=""
  if [[ "$unix_path" == *"/lib/gstreamer-1.0/"* ]]; then
    dest="$DIST_DIR/lib/gstreamer-1.0/$(basename "$unix_path")"
    mkdir -p "$DIST_DIR/lib/gstreamer-1.0"
  else
    dest="$DIST_DIR/$(basename "$unix_path")"
  fi

  if [ -f "$dest" ]; then
    return 0
  fi

  cp "$unix_path" "$dest"
}

while IFS= read -r dll_path; do
  dll_path="$(echo "$dll_path" | xargs)" # trim
  [ -z "$dll_path" ] && continue
  copy_one_dll "$dll_path"
done < "$DLL_LIST_FILE"

echo "Big runtime bundle copied (restricted to dll.txt)."

echo "================================================="
echo " 4b. Resolving GStreamer/FFmpeg runtime DLLs"
echo "================================================="

MSYS_PREFIX="${MSYSTEM_PREFIX:-/ucrt64}"

OBJDUMP="$MSYS_PREFIX/bin/objdump.exe"
if [ ! -x "$OBJDUMP" ]; then
  OBJDUMP="$(command -v objdump || true)"
fi
if [ -z "$OBJDUMP" ]; then
  echo "ERROR: objdump not found; cannot collect DLL dependencies" >&2
  exit 1
fi

collect_imports() {
  local bin="$1"
  [ -f "$bin" ] || return 0
  "$OBJDUMP" -p "$bin" 2>/dev/null \
    | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'
}

find_runtime_dll() {
  local name="$1"
  local dir
  for dir in "$DIST_DIR" "$DIST_DIR/lib/gstreamer-1.0" \
             "$MSYS_PREFIX/bin" "$MSYS_PREFIX/lib" \
             "$MSYS_PREFIX/lib/gstreamer-1.0"; do
    if [ -f "$dir/$name" ]; then
      printf '%s\n' "$dir/$name"
      return 0
    fi
  done
  return 1
}

queue=("$DIST_DIR/$EXE_NAME")
for bin in "$DIST_DIR"/lib/gstreamer-1.0/*.dll "$DIST_DIR"/*.dll; do
  [ -f "$bin" ] && queue+=("$bin")
done

processed=0
while [ "${#queue[@]}" -gt 0 ]; do
  bin="${queue[0]}"
  queue=("${queue[@]:1}")
  [ -f "$bin" ] || continue
  processed=$((processed + 1))
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    if [ -f "$DIST_DIR/$name" ] || [ -f "$DIST_DIR/lib/gstreamer-1.0/$name" ]; then
      continue
    fi
    src="$(find_runtime_dll "$name" || true)"
    [ -n "$src" ] || continue
    dest="$DIST_DIR/$(basename "$name")"
    cp "$src" "$dest"
    queue+=("$dest")
  done < <(collect_imports "$bin")
done
echo "  resolved $processed binaries; $(find "$DIST_DIR" -maxdepth 1 -name '*.dll' | wc -l) runtime DLLs"

echo "================================================="
echo " 5. Finalizing Qt Dependencies (windeployqt)"
echo "================================================="
windeployqt --no-translations --no-compiler-runtime \
  --dir "$DIST_DIR" "$DIST_DIR/$EXE_NAME"


echo "================================================="
echo " 6. Copying mDNSResponder.exe and dnssd.dll"
echo "================================================="
cp "./Bonjour SDK/Bin/x64/dnssd.dll" release/.
cp "./Bonjour SDK/Bin/x64/mDNSResponder.exe" release/.

find $PROJECT_ROOT -name compile_commands.json
cp ./build/compile_commands.json release/.


echo "================================================="
echo " 7. Copying more files over"
echo "================================================="
find $PROJECT_ROOT -name *.ico
mkdir -p release/resources
cp stuff/newicon.ico release/resources/icon.ico
cp stuff/uxplay_arguments_list.txt release/resources/uxplay_arguments_list.txt

echo "================================================="
echo " 8. Verifying bundled GStreamer plugins"
echo "================================================="

missing=0
for bin in "$DIST_DIR/$EXE_NAME" "$DIST_DIR"/*.dll "$DIST_DIR"/lib/gstreamer-1.0/*.dll; do
  [ -f "$bin" ] || continue
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    if [ -f "$DIST_DIR/$name" ] || [ -f "$DIST_DIR/lib/gstreamer-1.0/$name" ]; then
      continue
    fi
    src="$(find_runtime_dll "$name" || true)"
    if [ -n "$src" ]; then
      echo "ERROR: runtime DLL '$name' is missing from the bundle (source: $src)" >&2
      missing=1
    fi
  done < <(collect_imports "$bin")
done
if [ "$missing" -ne 0 ]; then
  exit 1
fi
echo "All non-system DLL imports are present in the bundle."

if command -v gst-inspect-1.0 >/dev/null 2>&1; then
  export GST_PLUGIN_PATH="$DIST_DIR/lib/gstreamer-1.0"
  export GST_PLUGIN_SYSTEM_PATH=""
  export PATH="$DIST_DIR:$PATH"

  gst-inspect-1.0 -b || true
  for feature in appsrc avdec_h264 playbin autovideosink h264parse textoverlay; do
    if ! gst-inspect-1.0 --exists "$feature" >/dev/null 2>&1; then
      echo "ERROR: required GStreamer feature '$feature' is missing from the bundle" >&2
      echo "       see the blacklist above; a runtime DLL is probably missing" >&2
      exit 1
    fi
  done
  echo "All required GStreamer features are loadable."
else
  echo "WARNING: gst-inspect-1.0 not found; skipping plugin smoke test" >&2
fi

echo "================================================="
echo " ✅ Done! Package is ready in $DIST_DIR"
echo "================================================="

sleep 1
echo ""
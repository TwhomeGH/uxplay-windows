"""Publish animated NV12 frames to the UxPlay virtual-camera pipe.

The virtual-camera media source (running in the Windows Frame Server) creates
``\\\\.\\pipe\\uxplay-vcam-frame-v1``.  This tool connects as a writer and sends
one complete frame per message, so the virtual camera can be verified without
involving AirPlay or GStreamer yet.

Usage:
    python virtualcam/tools/shared_frame_writer.py --seconds 30
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import struct
import time

MAGIC = 0x43565855  # "UXVC"
VERSION = 1
HEADER_SIZE = 48
HEADER = struct.Struct("<IIIIIIIIQQ")
PIPE_NAME = r"\\.\pipe\uxplay-vcam-frame-v1"

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.CreateFileW.argtypes = [
    wt.LPCWSTR,
    wt.DWORD,
    wt.DWORD,
    ctypes.c_void_p,
    wt.DWORD,
    wt.DWORD,
    wt.HANDLE,
]
_kernel32.CreateFileW.restype = wt.HANDLE
_kernel32.WriteFile.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
    ctypes.c_void_p,
]
_kernel32.WriteFile.restype = wt.BOOL
_kernel32.CloseHandle.argtypes = [wt.HANDLE]

GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = wt.HANDLE(-1).value


def _connect() -> int | None:
    handle = _kernel32.CreateFileW(
        PIPE_NAME,
        GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        0,
        None,
    )
    if handle == INVALID_HANDLE_VALUE or not handle:
        return None
    return handle


def _make_nv12(width: int, height: int, frame_id: int) -> bytes:
    """Return an animated NV12 frame with a moving bright vertical bar."""
    y_plane = bytearray(width * height)
    bar_x = (frame_id * 8) % width
    for y in range(height):
        row = y * width
        shade = 30 + (y * 160 // max(height - 1, 1))
        y_plane[row : row + width] = bytes([shade]) * width
        start = bar_x
        end = min(start + 24, width)
        y_plane[row + start : row + end] = b"\xeb" * (end - start)

    uv_plane = bytearray(width * height // 2)
    uv_plane[:] = b"\x80" * len(uv_plane)
    return bytes(y_plane) + bytes(uv_plane)


def _write_frame(handle: int, width: int, height: int, frame_id: int) -> bool:
    payload = _make_nv12(width, height, frame_id)
    header = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        width,
        height,
        width,
        2,  # NV12
        len(payload),
        frame_id,
        int(time.time() * 1000),
    )
    message = header + payload
    written = wt.DWORD(0)
    ok = _kernel32.WriteFile(
        handle,
        ctypes.c_char_p(message),
        len(message),
        ctypes.byref(written),
        None,
    )
    return bool(ok) and written.value == len(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()

    print("waiting for virtual camera frame pipe...", flush=True)
    handle = None
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        handle = _connect()
        if handle is not None:
            break
        time.sleep(0.2)
    if handle is None:
        print("frame pipe not found; start the virtual camera first", flush=True)
        return 1

    print(
        f"publishing {args.width}x{args.height} NV12 at {args.fps:g} fps",
        flush=True,
    )
    frame_id = int(time.time() * 1000) & 0xFFFFFFFF
    end = time.monotonic() + args.seconds
    period = 1.0 / max(args.fps, 1.0)

    try:
        while time.monotonic() < end:
            started = time.monotonic()
            frame_id += 1
            if not _write_frame(handle, args.width, args.height, frame_id):
                print("frame pipe disconnected", flush=True)
                break
            remaining = period - (time.monotonic() - started)
            if remaining > 0:
                time.sleep(remaining)
        print("writer finished", flush=True)
        return 0
    finally:
        _kernel32.CloseHandle(handle)


if __name__ == "__main__":
    raise SystemExit(main())

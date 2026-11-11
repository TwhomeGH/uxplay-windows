"""Probe the Apple USB Multiplexor WinUSB interface on Windows.

Read-only by default. The --enable/--disable flags send the same vendor
control request the QuickTime capture tools use to switch the device to its
QuickTime USB configuration (and back). No driver is replaced.
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import uuid
from ctypes import wintypes

APPLE_MUX_GUID = uuid.UUID("{664be590-54bd-4964-8a8c-6cd1314f6dc2}")
CM_GET_DEVICE_INTERFACE_LIST_PRESENT = 0x00000000
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

cfgmgr32 = ctypes.WinDLL("cfgmgr32")
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
winusb = ctypes.WinDLL("winusb", use_last_error=True)


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class WINUSB_SETUP_PACKET(ctypes.Structure):
    _fields_ = [
        ("RequestType", ctypes.c_ubyte),
        ("Request", ctypes.c_ubyte),
        ("Value", ctypes.c_ushort),
        ("Index", ctypes.c_ushort),
        ("Length", ctypes.c_ushort),
    ]


def make_guid(value: uuid.UUID) -> GUID:
    time_low, time_mid, time_hi_version, seq_hi, seq_low, node = value.fields
    data4 = bytes([seq_hi, seq_low]) + node.to_bytes(6, "big")
    return GUID(time_low, time_mid, time_hi_version, (ctypes.c_ubyte * 8)(*data4))


def interface_paths(guid: uuid.UUID) -> list[str]:
    native = make_guid(guid)
    size = wintypes.ULONG(0)
    result = cfgmgr32.CM_Get_Device_Interface_List_SizeW(
        ctypes.byref(size), ctypes.byref(native), None,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT,
    )
    if result != 0:
        raise OSError(f"CM_Get_Device_Interface_List_Size failed: {result}")
    buffer = ctypes.create_unicode_buffer(size.value)
    result = cfgmgr32.CM_Get_Device_Interface_ListW(
        ctypes.byref(native), None, buffer, size.value,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT,
    )
    if result != 0:
        raise OSError(f"CM_Get_Device_Interface_List failed: {result}")
    raw = bytes(buffer)
    text = raw.decode("utf-16-le", errors="ignore")
    return [item for item in text.split("\0") if item]


def open_winusb(path: str):
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    ]
    modes = [
        ("rw", GENERIC_READ | GENERIC_WRITE, 0),
        ("read", GENERIC_READ, 0),
        ("write", GENERIC_WRITE, 0),
        ("none", 0, 0),
        ("rw-overlapped", GENERIC_READ | GENERIC_WRITE, 0x40000000),
    ]
    handle = None
    for name, access, flags in modes:
        handle = kernel32.CreateFileW(
            path,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            None,
            OPEN_EXISTING,
            flags,
            None,
        )
        if handle not in (INVALID_HANDLE_VALUE, None):
            print(f"  access={name}: open ok")
            break
        print(
            f"  access={name}: error {ctypes.get_last_error()}"
        )
        handle = None
    if handle is None:
        raise OSError(ctypes.get_last_error(), "CreateFileW failed")

    winusb_handle = wintypes.HANDLE()
    winusb.WinUsb_Initialize.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(wintypes.HANDLE),
    ]
    if not winusb.WinUsb_Initialize(handle, ctypes.byref(winusb_handle)):
        error = ctypes.get_last_error()
        kernel32.CloseHandle(handle)
        raise OSError(error, "WinUsb_Initialize failed")
    return handle, winusb_handle


def control(winusb_handle, request_type: int, request: int,
            value: int, index: int, length: int = 0) -> int:
    packet = WINUSB_SETUP_PACKET(request_type, request, value, index, length)
    transferred = wintypes.ULONG(0)
    winusb.WinUsb_ControlTransfer.argtypes = [
        wintypes.HANDLE, WINUSB_SETUP_PACKET, ctypes.c_void_p,
        wintypes.ULONG, ctypes.POINTER(wintypes.ULONG), wintypes.LPVOID,
    ]
    if not winusb.WinUsb_ControlTransfer(
        winusb_handle, packet, None, 0,
        ctypes.byref(transferred), None,
    ):
        error = ctypes.get_last_error()
        raise OSError(error, "WinUsb_ControlTransfer failed")
    return transferred.value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--guid", default=str(APPLE_MUX_GUID))
    parser.add_argument("--contains", default="")
    parser.add_argument("--enable", action="store_true")
    parser.add_argument("--disable", action="store_true")
    args = parser.parse_args()

    paths = interface_paths(uuid.UUID(args.guid))
    if args.contains:
        paths = [path for path in paths if args.contains.lower() in path.lower()]
    print(f"paths={len(paths)}")
    for path in paths:
        print(f"  {path}")
    if not paths:
        return 1

    for path in paths:
        try:
            handle, winusb_handle = open_winusb(path)
        except OSError as exc:
            print(f"open failed: {exc}")
            continue
        print(f"open ok: {path}")
        if args.enable or args.disable:
            value = 2 if args.enable else 0
            try:
                sent = control(winusb_handle, 0x40, 0x52, 0x0000, value)
                print(f"vendor request 0x52 index={value} ok, transferred={sent}")
            except OSError as exc:
                print(f"vendor request failed: {exc}")
        winusb.WinUsb_Free(winusb_handle)
        kernel32.CloseHandle(handle)
    return 0


if __name__ == "__main__":
    sys.exit(main())

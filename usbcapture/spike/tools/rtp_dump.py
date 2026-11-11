"""Unwrap the length-prefixed RTP/HEVC capture from pymobiledevice3.

Input : [4-byte big-endian length][RTP/RTCP packet] ...
Output: Annex-B HEVC (.h265)

The device sends RTP/HEVC per RFC 7798: Aggregation Packets (nal_type 48),
Fragmentation Units (49), and single NAL units. This follows the reference
``misc/rtp_dump.py`` shipped with pymobiledevice3 (GPL-3.0).
"""

from __future__ import annotations

import argparse
import pathlib

START_CODE = b"\x00\x00\x00\x01"
H265_NAL_AP = 48
H265_NAL_FU = 49


def nal_type(payload: bytes) -> int:
    return (payload[0] >> 1) & 0x3F


def process_payload(payload: bytes, fu_buffer: bytearray,
                    out: list[bytes]) -> None:
    if len(payload) < 2:
        return
    kind = nal_type(payload)
    if kind == H265_NAL_AP:
        index = 2
        while index + 2 <= len(payload):
            size = int.from_bytes(payload[index:index + 2], "big")
            index += 2
            out.append(START_CODE + payload[index:index + size])
            index += size
    elif kind == H265_NAL_FU:
        fu_header = payload[2]
        start = fu_header & 0x80
        end = fu_header & 0x40
        original_type = fu_header & 0x3F
        if start:
            first = (payload[0] & 0x81) | (original_type << 1)
            fu_buffer[:] = bytes([first, payload[1]]) + payload[3:]
        else:
            fu_buffer.extend(payload[3:])
        if end and fu_buffer:
            out.append(START_CODE + bytes(fu_buffer))
            fu_buffer.clear()
    else:
        out.append(START_CODE + payload)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    data = args.input.read_bytes()
    index = 0
    chunks: list[bytes] = []
    fu_buffer = bytearray()
    rtp_count = 0
    rtcp_count = 0

    while index + 4 <= len(data):
        length = int.from_bytes(data[index:index + 4], "big")
        index += 4
        packet = data[index:index + length]
        index += length
        if len(packet) < 12:
            continue
        payload_type = packet[1] & 0x7F
        if 64 <= payload_type <= 95:
            rtcp_count += 1
            continue
        rtp_count += 1
        csrc_count = packet[0] & 0x0F
        header_len = 12 + csrc_count * 4
        if packet[0] & 0x10:
            ext_len = int.from_bytes(packet[header_len + 2:header_len + 4],
                                     "big")
            header_len += 4 + ext_len * 4
        process_payload(packet[header_len:], fu_buffer, chunks)

    args.output.write_bytes(b"".join(chunks))
    print(f"RTP: {rtp_count}, RTCP: {rtcp_count}")
    print(f"NAL units: {len(chunks)}")
    print(f"Wrote {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

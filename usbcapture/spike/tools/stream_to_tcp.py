"""Relay pymobiledevice3 ``/stream.bin`` to a TCP socket as Annex-B HEVC.

This is the bridge for a native renderer: the sidecar owns the RSD tunnel and
the HTTP server, while this process turns the framed HEVC access units into a
plain byte stream that GStreamer ``tcpclientsrc ! h265parse`` can decode.

The sidecar must already be running::

    pymobiledevice3 developer core-device display serve-web --http-port 8099

Usage::

    python stream_to_tcp.py --http-port 8099 --tcp-port 5005
"""

from __future__ import annotations

import argparse
import base64
import http.client
import json
import socket
import sys

START_CODE = b"\x00\x00\x00\x01"


def fetch_codec(port: int) -> dict:
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
    try:
        conn.request("GET", "/codec")
        response = conn.getresponse()
        body = response.read()
        if response.status != 200:
            raise RuntimeError(f"/codec HTTP {response.status}: {body[:200]!r}")
        return json.loads(body)
    finally:
        conn.close()


def hvcC_parameter_sets(description: str) -> list[bytes]:
    """Extract VPS/SPS/PPS NAL units from an HEVCDecoderConfigurationRecord."""
    data = base64.b64decode(description)
    if len(data) < 23:
        return []
    parameter_sets = []
    index = 23
    for _ in range(data[22]):
        if index + 3 > len(data):
            break
        count = int.from_bytes(data[index + 1:index + 3], "big")
        index += 3
        for _ in range(count):
            if index + 2 > len(data):
                break
            size = int.from_bytes(data[index:index + 2], "big")
            index += 2
            parameter_sets.append(data[index:index + size])
            index += size
    return parameter_sets


def read_exact(response: http.client.HTTPResponse, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = response.read(size - len(buf))
        if not chunk:
            raise EOFError("stream ended")
        buf.extend(chunk)
    return bytes(buf)


def annex_b_parameter_sets(description: str) -> bytes:
    return b"".join(START_CODE + nal for nal in hvcC_parameter_sets(description))


def access_unit_to_annex_b(payload: bytes) -> bytes:
    out = bytearray()
    index = 0
    while index + 4 <= len(payload):
        size = int.from_bytes(payload[index:index + 4], "big")
        index += 4
        out += START_CODE + payload[index:index + size]
        index += size
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--http-port", type=int, default=8099)
    parser.add_argument("--tcp-port", type=int, default=5005)
    args = parser.parse_args()

    codec = fetch_codec(args.http_port)
    print(f"codec={codec.get('codec')} display={codec.get('display')}",
          file=sys.stderr)
    prefix = annex_b_parameter_sets(codec["description"])
    print(f"parameter sets={len(prefix)} bytes", file=sys.stderr)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.tcp_port))
    server.listen(1)
    print(f"waiting for GStreamer on tcp://127.0.0.1:{args.tcp_port}",
          file=sys.stderr)

    client, address = server.accept()
    print(f"client {address}", file=sys.stderr)

    conn = http.client.HTTPConnection("127.0.0.1", args.http_port, timeout=30)
    conn.request("GET", "/stream.bin")
    response = conn.getresponse()
    if response.status != 200:
        raise RuntimeError(f"/stream.bin HTTP {response.status}")

    frames = 0
    try:
        while True:
            length = int.from_bytes(read_exact(response, 4), "big")
            message = read_exact(response, length)
            kind, data = message[0], message[1:]
            if kind == 3:
                info = json.loads(data)
                prefix = annex_b_parameter_sets(info["description"])
                print(f"decoder config: {info.get('codec')}", file=sys.stderr)
                continue
            if kind == 4:
                print(f"rotation: {json.loads(data)!r}", file=sys.stderr)
                continue
            out = access_unit_to_annex_b(data)
            if kind in (0, 2) and prefix:
                out = prefix + out
            client.sendall(out)
            frames += 1
            if frames % 120 == 0:
                print(f"frames={frames}", file=sys.stderr)
    except (BrokenPipeError, ConnectionResetError, EOFError) as exc:
        print(f"stopped: {exc}", file=sys.stderr)
    finally:
        response.close()
        conn.close()
        client.close()
        server.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

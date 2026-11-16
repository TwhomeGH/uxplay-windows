#!/usr/bin/env python3
"""uxplay-windows diagnostics viewer.

Read-only local viewer for a diagnostics bundle or the current application log.
It never uploads data and binds to 127.0.0.1 by default.

Usage:
    python scripts/diagnostic_viewer.py
    python scripts/diagnostic_viewer.py <bundle-dir-or-zip>
    python scripts/diagnostic_viewer.py <uxplay-windows.log> --port 8766
"""

from __future__ import annotations

import argparse
import errno
import json
import re
import sys
import tempfile
import webbrowser
import zipfile
from collections import Counter
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent
TEMPLATE = ROOT / "diagnostic_viewer.html"
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8766
MAX_LOG_EVENTS = 12000

LOG_LINE = re.compile(
    r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\] "
    r"\[(DEBUG|INFO|WARN|ERROR|FATAL)\] (.*)$"
)
CATEGORY_PREFIX = re.compile(r"^\[([^\]]+)\]\s*(.*)$")
FIELD = re.compile(r"\b([A-Za-z_][A-Za-z0-9_.-]*)=([^\s]+)")
ERROR_WORDS = re.compile(
    r"\b(error|failed|failure|fatal|panic|crash|exception|abort|"
    r"segmentation|denied)\b",
    re.IGNORECASE,
)
WARN_WORDS = re.compile(
    r"\b(warn|warning|low_fps|timeout|stale|dropped|rejected|"
    r"unavailable|not found)\b",
    re.IGNORECASE,
)


@dataclass
class Event:
    ts: str
    category: str
    severity: str
    message: str
    source: str
    fields: dict[str, str] = field(default_factory=dict)

    def as_dict(self) -> dict:
        return {
            "ts": self.ts,
            "category": self.category,
            "severity": self.severity,
            "message": self.message,
            "source": self.source,
            "fields": self.fields,
        }


@dataclass
class Metric:
    ts: str
    values: dict[str, object]

    def as_dict(self) -> dict:
        return {"ts": self.ts, **self.values}


def classify(level: str, category: str, message: str) -> str:
    level = (level or "").upper()
    if level in {"ERROR", "FATAL"}:
        return "error"
    if level == "WARN":
        return "warn"
    if category == "video" and "low_fps" in message.lower():
        return "warn"
    if category == "client" and "rejected" in message.lower():
        return "warn"
    if ERROR_WORDS.search(message):
        return "error"
    if WARN_WORDS.search(message):
        return "warn"
    return "info"


def category_for(message: str) -> tuple[str, str]:
    match = CATEGORY_PREFIX.match(message)
    if match:
        tag = match.group(1).strip()
        body = match.group(2).strip()
        if tag == "engine":
            return "engine", body
        if tag == "stats":
            return "video", body
        if tag:
            return tag, body
    lowered = message.lower()
    if lowered.startswith("client connected"):
        return "client", message
    if lowered.startswith("client disconnected"):
        return "client", message
    if lowered.startswith("client rejected"):
        return "client", message
    if lowered.startswith("uxplay engine ready"):
        return "engine", "ready"
    if lowered.startswith("uxplay engine stopped"):
        return "engine", "stopped"
    if "starting uxplay engine" in lowered:
        return "engine", "starting"
    return "app", message


def parse_log(path: Path) -> list[Event]:
    events: list[Event] = []
    try:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return events
    for raw in text.splitlines()[-MAX_LOG_EVENTS:]:
        match = LOG_LINE.match(raw)
        if not match:
            continue
        ts, level, body = match.groups()
        category, message = category_for(body)
        fields = {}
        for key, value in FIELD.findall(body):
            if len(value) <= 160:
                fields[key] = value
        events.append(
            Event(
                ts=ts,
                category=category,
                severity=classify(level, category, body),
                message=message,
                source="log",
                fields=fields,
            )
        )
    return events


def parse_events(path: Path) -> list[Event]:
    events: list[Event] = []
    if not path.is_file():
        return events
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        data = record.get("data") or {}
        fields = {}
        if isinstance(data, dict):
            for key, value in data.items():
                fields[str(key)] = str(value)
        category = str(record.get("category") or "app")
        message = str(record.get("message") or "")
        events.append(
            Event(
                ts=str(record.get("ts") or ""),
                category=category,
                severity=classify("", category, message),
                message=message,
                source="event",
                fields=fields,
            )
        )
    return events


def parse_metrics(path: Path) -> list[Metric]:
    metrics: list[Metric] = []
    if not path.is_file():
        return metrics
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        ts = str(record.pop("ts", ""))
        metrics.append(Metric(ts=ts, values=record))
    return metrics


def latest_diagnostics_dir() -> Path | None:
    import os

    base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
    if not base:
        return None
    root = Path(base) / "leapbtw" / "uxplay-windows" / "diagnostics"
    if not root.is_dir():
        return None
    candidates = [p for p in root.iterdir() if p.is_dir() or p.suffix == ".zip"]
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def resolve_input(value: str | None) -> tuple[Path, Path | None]:
    """Return (bundle-or-log path, temporary extraction directory)."""
    if value:
        path = Path(value).expanduser().resolve()
    else:
        latest = latest_diagnostics_dir()
        if latest is None:
            raise SystemExit("找不到 diagnostics bundle；請指定 log 或 bundle 路徑。")
        path = latest
    if not path.exists():
        raise SystemExit(f"找不到路徑：{path}")
    if path.is_file() and path.suffix.lower() == ".zip":
        temp = Path(tempfile.mkdtemp(prefix="uxplay-diagnostics-"))
        with zipfile.ZipFile(path) as archive:
            archive.extractall(temp)
        # tar/Compress-Archive may place the bundle in a top-level folder.
        children = [child for child in temp.iterdir() if child.is_dir()]
        if len(children) == 1 and (children[0] / "manifest.json").exists():
            return children[0], temp
        return temp, temp
    return path, None


def load_log_from_bundle(bundle: Path) -> Path | None:
    logs = bundle / "logs"
    if not logs.is_dir():
        return None
    candidates = sorted(logs.glob("*.log"))
    return candidates[0] if candidates else None


def load_bundle(path: Path) -> dict:
    if path.is_file() and path.suffix.lower() == ".log":
        events = parse_log(path)
        return {
            "path": path,
            "manifest": {},
            "events": events,
            "metrics": [],
            "summary": summarize(path, events, []),
        }

    manifest_path = path / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        manifest = {}
    events = parse_events(path / "events.jsonl")
    if not events:
        timeline = path / "timeline.txt"
        if timeline.is_file():
            events = parse_timeline(timeline)
    log_path = load_log_from_bundle(path)
    if log_path:
        events.extend(parse_log(log_path))
    metrics = parse_metrics(path / "metrics.jsonl")
    return {
        "path": path,
        "manifest": manifest,
        "events": events,
        "metrics": metrics,
        "summary": summarize(path, events, metrics, manifest),
    }


def parse_timeline(path: Path) -> list[Event]:
    events: list[Event] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^(\S+) \[([^\]]+)\] (.*)$", raw)
        if not match:
            continue
        ts, category, message = match.groups()
        fields = {}
        for key, value in FIELD.findall(message):
            fields[key] = value
        events.append(
            Event(
                ts=ts,
                category=category,
                severity=classify("", category, message),
                message=message,
                source="timeline",
                fields=fields,
            )
        )
    return events


def summarize(path: Path, events: list[Event], metrics: list[Metric],
              manifest: dict | None = None) -> dict:
    manifest = manifest or {}
    categories = Counter(event.category for event in events)
    severities = Counter(event.severity for event in events)
    times = sorted(event.ts for event in events if event.ts)
    low_fps = sum(
        1 for event in events
        if event.category == "video" and "low_fps" in event.message.lower()
    )
    connects = sum(
        1 for event in events
        if event.category == "client"
        and (event.message.lower().startswith("client connected")
             or event.message == "connected")
    )
    disconnects = sum(
        1 for event in events
        if event.category == "client"
        and (event.message.lower().startswith("client disconnected")
             or event.message == "disconnected")
    )
    return {
        "path": str(path),
        "name": path.name,
        "generated": manifest.get("created_at", ""),
        "app_version": manifest.get("app_version", ""),
        "git_sha": manifest.get("git_sha", ""),
        "build_type": manifest.get("build_type", ""),
        "range": f"{times[0]} ~ {times[-1]}" if times else "無時間戳",
        "events": len(events),
        "metrics": len(metrics),
        "errors": severities.get("error", 0),
        "warnings": severities.get("warn", 0),
        "low_fps": low_fps,
        "connects": connects,
        "disconnects": disconnects,
        "categories": [
            {"name": name, "count": count}
            for name, count in categories.most_common()
        ],
        "last_status": manifest.get("last_status", ""),
        "last_client": manifest.get("last_client", {}),
    }


class ViewerHandler(BaseHTTPRequestHandler):
    dataset: dict = {}

    def log_message(self, format: str, *args) -> None:  # noqa: A002,A003
        return

    def send_json(self, value: object, status: int = 200) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_html(self) -> None:
        try:
            body = TEMPLATE.read_bytes()
        except OSError as error:
            self.send_error(500, f"找不到 diagnostic_viewer.html: {error}")
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/index.html"):
            self.send_html()
            return
        if parsed.path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        if parsed.path == "/api/summary":
            dataset = self.dataset
            summary = dict(dataset["summary"])
            summary["metrics_preview"] = [
                metric.as_dict() for metric in dataset["metrics"][-120:]
            ]
            self.send_json(summary)
            return
        if parsed.path == "/api/events":
            self.handle_events(parse_qs(parsed.query))
            return
        if parsed.path == "/api/metrics":
            self.send_json({"metrics": [
                metric.as_dict() for metric in self.dataset["metrics"][-600:]
            ]})
            return
        self.send_error(404)

    def handle_events(self, query: dict[str, list[str]]) -> None:
        def first(name: str) -> str:
            return (query.get(name) or [""])[0]

        category = first("category")
        severity = first("severity")
        source = first("source")
        search = first("q").lower()
        try:
            offset = max(0, int(first("offset") or "0"))
            limit = min(1000, max(1, int(first("limit") or "300")))
        except ValueError:
            self.send_json({"error": "offset/limit 必須是整數"}, 400)
            return

        def matches(event: dict) -> bool:
            if category and event["category"] != category:
                return False
            if severity and event["severity"] != severity:
                return False
            if source and event["source"] != source:
                return False
            if search:
                haystack = " ".join(
                    [event["message"], event["category"], event["ts"]]
                    + [f"{k}={v}" for k, v in event["fields"].items()]
                ).lower()
                if search not in haystack:
                    return False
            return True

        selected = []
        for event in self.dataset["events"]:
            item = event.as_dict()
            if matches(item):
                selected.append(item)
        page = selected[offset:offset + limit]
        self.send_json({
            "total": len(selected),
            "offset": offset,
            "limit": limit,
            "hasMore": offset + len(page) < len(selected),
            "events": page,
        })


def bind_server(host: str, start_port: int, attempts: int = 20) -> ThreadingHTTPServer:
    last_error = None
    for offset in range(attempts):
        candidate = 0 if start_port == 0 else start_port + offset
        try:
            server = ThreadingHTTPServer((host, candidate), ViewerHandler)
            if offset:
                print(f"Port {start_port} 已被占用，改用 {server.server_address[1]}。")
            return server
        except OSError as error:
            last_error = error
            in_use = error.errno in (errno.EADDRINUSE, errno.EACCES, 10048)
            in_use = in_use or getattr(error, "winerror", None) == 10013
            if start_port == 0 or not in_use:
                raise
    raise SystemExit(f"Port {start_port}~{start_port + attempts - 1} 都無法使用：{last_error}")


def main() -> None:
    if sys.platform == "win32":
        for stream in (sys.stdout, sys.stderr):
            reconfigure = getattr(stream, "reconfigure", None)
            if reconfigure is not None:
                reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description="uxplay-windows diagnostics viewer")
    parser.add_argument("path", nargs="?", help="bundle folder/zip, or log file")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    path, temp_dir = resolve_input(args.path)
    try:
        dataset = load_bundle(path)
        ViewerHandler.dataset = dataset
        summary = dataset["summary"]
        server = bind_server(args.host, args.port)
        url = f"http://{args.host}:{server.server_address[1]}/"
        print(f"診斷檢視器：{url}")
        print(f"資料：{path}（{summary['events']} events，{summary['metrics']} metrics）")
        print("按 Ctrl+C 停止。")
        if not args.no_browser:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\n已停止。")
        finally:
            server.server_close()
    finally:
        if temp_dir is not None:
            import shutil
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()

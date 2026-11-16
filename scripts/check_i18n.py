#!/usr/bin/env python3
"""Validate uxplay-windows translation catalogs.

Source language is English. A missing translation is valid at runtime (Qt
falls back to the English source), but it must be visible in this report so it
is not silently forgotten.

Usage:
    python scripts/check_i18n.py
    python scripts/check_i18n.py --allow-missing
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "translations" / "i18n.json"
PLACEHOLDER = re.compile(r"%(?:L?\d+|n|%)")

BARE_UI_PATTERNS = [
    (re.compile(r'\b(?:new\s+)?(?:QLabel|QPushButton|QCheckBox|QGroupBox|QLineEdit)\s*\(\s*"((?:\\.|[^"\\])*)"', re.S),
     "widget constructor"),
    (re.compile(r'(?:\.|->)(?:setText|setToolTip|setPlaceholderText|setWindowTitle|setStatusTip|setAccessibleName)\s*\(\s*"((?:\\.|[^"\\])*)"', re.S),
     "setter"),
    (re.compile(r'(?:\.|->)(?:addAction|addItem)\s*\(\s*"((?:\\.|[^"\\])*)"', re.S),
     "menu/item"),
    (re.compile(r'\bshowMessage\s*\(\s*"((?:\\.|[^"\\])*)"', re.S),
     "tray message title"),
    (re.compile(r'\bQMessageBox::(?:information|warning|critical|question)\s*\(\s*[^,]+,\s*"((?:\\.|[^"\\])*)"', re.S),
     "message box title"),
    (re.compile(r'(?:\.|->)(?:setText|setToolTip|setPlaceholderText|setWindowTitle|setStatusTip|setAccessibleName)\s*\(\s*QStringLiteral\(\s*"((?:\\.|[^"\\])*)"', re.S),
     "QStringLiteral setter"),
]

TECHNICAL_LITERAL = re.compile(r"^(?:\d+\s*x\s*\d+|\d+p|\d+k)$", re.I)

RESET = "\033[0m"
RED = "\033[91m"
YELLOW = "\033[93m"
GREEN = "\033[92m"
CYAN = "\033[96m"


def enable_windows_ansi() -> None:
    if sys.platform != "win32":
        return
    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        for handle_id in (-11, -12):  # stdout, stderr
            handle = kernel32.GetStdHandle(handle_id)
            mode = ctypes.c_uint32()
            if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                kernel32.SetConsoleMode(handle, mode.value | 0x0004)
    except (AttributeError, OSError):
        pass


class Colors:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def paint(self, text: str, color: str) -> str:
        return f"{color}{text}{RESET}" if self.enabled else text

    def error(self, text: str) -> str:
        return self.paint(f"[ERROR] {text}", RED)

    def warning(self, text: str) -> str:
        return self.paint(f"[WARN] {text}", YELLOW)

    def success(self, text: str) -> str:
        return self.paint(f"[OK] {text}", GREEN)

    def info(self, text: str) -> str:
        return self.paint(f"[INFO] {text}", CYAN)


def load_config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8"))


def find_lupdate(explicit: str | None = None) -> str | None:
    """Find lupdate; Windows PowerShell often does not have Qt on PATH."""
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    env_value = os.environ.get("LUPDATE")
    if env_value:
        candidates.append(env_value)
    which = shutil.which("lupdate")
    if which:
        candidates.append(which)

    qt_root = os.environ.get("QTDIR") or os.environ.get("QT_ROOT")
    if qt_root:
        candidates.append(str(Path(qt_root) / "bin" / "lupdate.exe"))
        candidates.append(str(Path(qt_root) / "bin" / "lupdate"))

    msys_root = os.environ.get("MSYS2_ROOT")
    if msys_root:
        for environment in ("ucrt64", "clang64", "mingw64"):
            candidates.append(str(Path(msys_root) / environment / "bin" / "lupdate.exe"))

    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def source_strings_from_lupdate(lupdate: str) -> set[str] | None:
    """Use lupdate so concatenated and translated strings match exactly."""
    if not lupdate:
        return None
    try:
        with tempfile.TemporaryDirectory(prefix="uxplay-i18n-") as directory:
            target = Path(directory) / "check.ts"
            result = subprocess.run(
                [lupdate, str(ROOT / "src"), "-ts", str(target), "-no-obsolete"],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=120,
            )
            if result.returncode != 0 or not target.is_file():
                return None
            root = ET.parse(target).getroot()
    except (OSError, subprocess.SubprocessError, ET.ParseError):
        return None
    strings: set[str] = set()
    for context in root.findall("context"):
        for message in context.findall("message"):
            source = message.findtext("source")
            if source:
                strings.add(source)
    return strings


def check_catalog(path: Path, source_strings: set[str] | None) -> list[str]:
    errors: list[str] = []
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as error:
        return [f"{path.name}: cannot parse: {error}"]

    seen: dict[tuple[str, str], int] = {}
    catalog_sources: set[str] = set()
    total = 0
    translated = 0

    for context in root.findall("context"):
        context_name = context.findtext("name") or "?"
        for message in context.findall("message"):
            total += 1
            source = message.findtext("source") or ""
            translation = message.find("translation")
            catalog_sources.add(source)
            key = (context_name, source)
            seen[key] = seen.get(key, 0) + 1
            if seen[key] > 1:
                errors.append(f"{path.name}: duplicate message {context_name}: {source!r}")

            if translation is None:
                errors.append(f"{path.name}: missing <translation>: {context_name}: {source!r}")
                continue
            value = (translation.text or "").strip()
            unfinished = translation.get("type") == "unfinished"
            if unfinished or not value:
                errors.append(
                    f"{path.name}: untranslated {context_name}: {source!r}"
                )
            else:
                translated += 1
                missing_placeholders = (
                    Counter(PLACEHOLDER.findall(source))
                    - Counter(PLACEHOLDER.findall(value))
                )
                if missing_placeholders:
                    errors.append(
                        f"{path.name}: placeholder mismatch {context_name}: "
                        f"{source!r} -> {value!r}"
                    )
                extra_placeholders = (
                    Counter(PLACEHOLDER.findall(value))
                    - Counter(PLACEHOLDER.findall(source))
                )
                if extra_placeholders:
                    errors.append(
                        f"{path.name}: unexpected placeholder {context_name}: "
                        f"{source!r} -> {value!r}"
                    )

    if source_strings is not None:
        for source in sorted(catalog_sources - source_strings):
            errors.append(f"{path.name}: obsolete catalog key: {source!r}")
        for source in sorted(source_strings - catalog_sources):
            errors.append(f"{path.name}: source key not in catalog: {source!r}")

    print(f"{path.name}: {translated}/{total} translated")
    return errors


def check_bare_ui_strings(config: dict) -> list[str]:
    """Catch user-visible literals that never passed through tr()."""
    allow = {
        str(value) for value in config.get("technical_literals", [])
    }
    errors: list[str] = []
    for path in sorted((ROOT / "src").glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for pattern, description in BARE_UI_PATTERNS:
            for match in pattern.finditer(text):
                value = match.group(1)
                if not value.strip() or value in allow:
                    continue
                if TECHNICAL_LITERAL.fullmatch(value.strip()):
                    continue
                line = text[:match.start()].count("\n") + 1
                errors.append(
                    f"{path.name}:{line}: bare {description}: {value!r} "
                    f"(use tr() or add to technical_literals)"
                )
    return errors


def main() -> None:
    if sys.platform == "win32":
        for stream in (sys.stdout, sys.stderr):
            reconfigure = getattr(stream, "reconfigure", None)
            if reconfigure is not None:
                reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description="Check uxplay-windows translations")
    parser.add_argument(
        "--allow-missing",
        action="store_true",
        help="report incomplete entries but do not fail on them",
    )
    parser.add_argument(
        "--allow-bare",
        action="store_true",
        help="report bare UI strings but do not fail on them",
    )
    parser.add_argument(
        "--lupdate",
        help="path to lupdate.exe (otherwise auto-detect PATH/QTDIR/MSYS2_ROOT)",
    )
    parser.add_argument(
        "--require-lupdate",
        action="store_true",
        help="fail instead of skipping source-key comparison when lupdate is missing",
    )
    parser.add_argument(
        "--color",
        choices=("auto", "always", "never"),
        default="auto",
        help="ANSI colors: auto (default), always, never",
    )
    args = parser.parse_args()
    color_enabled = args.color == "always" or (
        args.color == "auto" and sys.stdout.isatty()
    )
    if color_enabled:
        enable_windows_ansi()
    colors = Colors(color_enabled)

    config = load_config()
    lupdate = find_lupdate(args.lupdate)
    source_strings = source_strings_from_lupdate(lupdate) if lupdate else None
    if source_strings is None:
        if args.require_lupdate:
            print(
                colors.error(
                    "lupdate is required for source-key comparison; "
                    "install Qt LinguistTools or pass --lupdate PATH."),
                file=sys.stderr,
            )
            raise SystemExit(2)
        print(
            colors.warning(
                "lupdate not found; skipping source-key comparison. "
                "Install Qt LinguistTools or pass --lupdate PATH."),
            file=sys.stderr,
        )

    names = {language["code"] for language in config.get("languages", [])}
    if config.get("fallback_language") not in names | {config.get("source_language")}:
        print("i18n.json: fallback language is not declared.", file=sys.stderr)
        raise SystemExit(2)

    catalog_errors: list[str] = []
    for language in config.get("languages", []):
        path = ROOT / "translations" / language["file"]
        catalog_errors.extend(check_catalog(path, source_strings))
    bare_errors = check_bare_ui_strings(config)
    all_errors = catalog_errors + bare_errors

    if all_errors:
        for error in all_errors:
            print(f"  {colors.error(error)}")
        summary = (
            f"FAILED: {len(all_errors)} issue(s) "
            f"(catalog {len(catalog_errors)}, bare {len(bare_errors)})"
        )
        print(colors.error(summary), file=sys.stderr)
        if catalog_errors and not args.allow_missing:
            raise SystemExit(1)
        if bare_errors and not args.allow_bare:
            raise SystemExit(1)
        print(colors.warning("issues reported; non-fatal because of allow flags"))
    else:
        print(colors.success("PASS: translations are complete."))


if __name__ == "__main__":
    main()

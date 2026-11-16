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
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "translations" / "i18n.json"
TR_CALL = re.compile(r'tr\(\s*"((?:\\.|[^"\\])*)"', re.S)


def load_config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8"))


def source_strings_from_lupdate() -> set[str] | None:
    """Use lupdate when available so concatenated tr() strings match exactly."""
    lupdate = shutil.which("lupdate")
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


def source_strings_from_regex() -> set[str]:
    strings: set[str] = set()
    for path in sorted((ROOT / "src").glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in TR_CALL.finditer(text):
            value = match.group(1)
            value = value.encode("utf-8").decode("unicode_escape")
            strings.add(value)
    return strings


def check_catalog(path: Path, source_strings: set[str]) -> list[str]:
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

    for source in sorted(source_strings - catalog_sources):
        errors.append(f"{path.name}: source key not in catalog: {source!r}")
    for source in sorted(catalog_sources - source_strings):
        errors.append(f"{path.name}: obsolete catalog key: {source!r}")

    print(f"{path.name}: {translated}/{total} translated")
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
        help="report incomplete entries but exit 0",
    )
    args = parser.parse_args()

    config = load_config()
    source_strings = source_strings_from_lupdate()
    if source_strings is None:
        source_strings = source_strings_from_regex()
        print("lupdate not found; source-key scan is regex-based.", file=sys.stderr)

    names = {language["code"] for language in config.get("languages", [])}
    if config.get("fallback_language") not in names | {config.get("source_language")}:
        print("i18n.json: fallback language is not declared.", file=sys.stderr)
        raise SystemExit(2)

    all_errors: list[str] = []
    for language in config.get("languages", []):
        path = ROOT / "translations" / language["file"]
        all_errors.extend(check_catalog(path, source_strings))

    if all_errors:
        print("\n".join(f"  {error}" for error in all_errors))
        print(f"\n{len(all_errors)} translation issue(s).", file=sys.stderr)
        if not args.allow_missing:
            raise SystemExit(1)
    else:
        print("Translations are complete.")


if __name__ == "__main__":
    main()

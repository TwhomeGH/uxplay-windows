"""PowerShell 入口與 CI 共用的增量 Doxygen 建置協調器。

結束碼：0 為最新／成功，1 為檢查發現過期，2 為建置或設定錯誤。
兩套文件驗證後才替換成功產物；前一版及失敗候選保留在 build/doxygen/runs。
"""
import argparse
from contextlib import contextmanager
import fnmatch
import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent


def digest(value):
    """對 bytes 或可序列化資料計算穩定的 SHA-256。"""
    if not isinstance(value, bytes):
        value = json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(value).hexdigest()


def tree_files(directory):
    """計算真實產物內容雜湊；拒絕符號連結，避免發佈越界。"""
    result = {}
    if directory.is_symlink():
        raise ValueError(f"Generated directory is a symlink: {directory}")
    if directory.is_dir():
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Generated output contains a symlink: {path}")
            if path.is_file():
                result[path.relative_to(directory).as_posix()] = digest(path.read_bytes())
    return result


def output_metadata(directory):
    """記錄產物大小與修改時間，供下次快速檢查使用；來源仍一律讀內容雜湊。"""
    return {path.relative_to(directory).as_posix(): [path.stat().st_size, path.stat().st_mtime_ns]
            for path in directory.rglob("*") if path.is_file()}


def cached_output_files(directory, hashes, metadata):
    """檢查缺檔及新增檔；產物 metadata 未變時沿用 SHA，變動才讀取內容。"""
    result = {}
    if directory.is_symlink():
        raise ValueError(f"Generated directory is a symlink: {directory}")
    if not directory.is_dir():
        return result
    for path in directory.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Generated output contains a symlink: {path}")
        if path.is_file():
            name = path.relative_to(directory).as_posix()
            info = path.stat()
            unchanged = metadata.get(name) == [info.st_size, info.st_mtime_ns]
            result[name] = hashes[name] if unchanged and name in hashes else digest(path.read_bytes())
    return result


def input_files(project):
    """依專案 Doxyfile 的固定輸入與排除規則計算來源內容雜湊。

    不支援的動態 include 或環境展開會明確拒絕，避免未檢查完整來源就誤判最新。
    """
    config = project / "Doxyfile"
    text = config.read_text(encoding="utf-8-sig")
    fields = {}
    for line in text.replace("\\\n", " ").splitlines():
        if re.match(r"^\s*@INCLUDE", line):
            raise ValueError(f"Dynamic Doxyfile inputs require explicit support: {config}")
        match = re.match(r"^([A-Z_]+)\s*=\s*(.*)$", line)
        if match:
            values = shlex.split(match[2], comments=True)
            if any("$(" in value for value in values):
                raise ValueError(f"Dynamic Doxyfile values require explicit support: {config}")
            fields[match[1]] = values
    excluded = [(project / name).resolve() for name in fields.get("EXCLUDE", [])]
    patterns = fields.get("FILE_PATTERNS", ["*.h", "*.cpp"])
    result = {"Doxyfile": digest(config.read_bytes())}
    for name in fields.get("INPUT", []):
        source = project / name
        if not source.exists():
            raise ValueError(f"Missing Doxygen input: {source}")
        recursive = fields.get("RECURSIVE", ["NO"]) == ["YES"]
        paths = (source.rglob("*") if recursive else source.glob("*")) if source.is_dir() else [source]
        for path in paths:
            if not path.is_file():
                continue
            resolved = path.resolve()
            if any(resolved == item or item in resolved.parents for item in excluded):
                continue
            if any(fnmatch.fnmatch(path.as_posix(), rule)
                   for rule in fields.get("EXCLUDE_PATTERNS", [])):
                continue
            if source.is_dir() and not any(fnmatch.fnmatch(path.name, rule) for rule in patterns):
                continue
            result[path.relative_to(project).as_posix()] = digest(path.read_bytes())
    return result


def api_symbols(tagfile):
    """擷取索引內的宣告及多載；不宣稱進行完整 ABI 分析。"""
    symbols = {}
    for compound in ET.parse(tagfile).getroot().findall("compound"):
        owner = compound.findtext("name", "")
        for member in compound.findall("member"):
            key = f"{compound.get('kind')}:{owner}::{member.get('kind')}:{member.findtext('name', '')}"
            declaration = " ".join((member.findtext("type", "") + " "
                                     + member.findtext("arglist", "")).split())
            symbols.setdefault(key, set()).add(declaration)
    return {key: sorted(values) for key, values in sorted(symbols.items())}


def api_diff(before, after):
    """分別列出新增、移除及變更的索引宣告群組。"""
    return {
        "added": sorted(after.keys() - before.keys()),
        "removed": sorted(before.keys() - after.keys()),
        "changed": sorted(key for key in before.keys() & after.keys() if before[key] != after[key]),
    }


def safe_path(path, boundary):
    """檢查產物絕對路徑，拒絕越界或經過符號連結的目標。"""
    boundary = boundary.absolute()
    path = path.absolute()
    if path == boundary or not path.is_relative_to(boundary):
        raise ValueError(f"Output outside build boundary: {path}")
    for parent in [boundary, *path.relative_to(boundary).parents]:
        candidate = parent if parent.is_absolute() else boundary / parent
        if candidate.is_symlink():
            raise ValueError(f"Symlinked output boundary: {candidate}")
    if path.is_symlink() or not path.resolve().is_relative_to(boundary.resolve()):
        raise ValueError(f"Unsafe generated output target: {path}")
    return path


def replace_generated(source, destination):
    """短暫檔案鎖定時重試搬移；持續拒絕仍失敗並交由呼叫端還原。"""
    for attempt in range(10):
        try:
            os.replace(source, destination)
            return
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(0.1)


def publish(sites, run, boundary):
    """先備妥完整目錄再啟用；舊產物歸檔，替換失敗時還原。

    所有目標均須在工作區內。前一版與失敗的產物保留在本次 build 目錄，不永久刪除。
    """
    prepared = []
    for index, (source, destination) in enumerate(sites):
        destination = safe_path(destination, boundary)
        staging = safe_path(destination.parent / f".publish-{run.name}-{index}", boundary)
        staging.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, staging)
        prepared.append((staging, destination, run / "previous" / str(index)))
    activated = []
    try:
        for staging, destination, previous in prepared:
            existed = destination.exists()
            previous.parent.mkdir(parents=True, exist_ok=True)
            activated.append((destination, previous, existed))
            if existed:
                replace_generated(destination, previous)
            replace_generated(staging, destination)
    except OSError:
        for index, (destination, previous, existed) in reversed(list(enumerate(activated))):
            if previous.exists() or not existed:
                if destination.exists():
                    replace_generated(destination, run / f"failed-publish-{index}")
                if previous.exists():
                    replace_generated(previous, destination)
        raise


def diagnostic(project, level, message):
    """輸出 VSCode「問題」面板可辨識的檔案、行號與嚴重度。"""
    print(f"{project / 'Doxyfile'}:1:1: {level}: {message}", flush=True)


def generate(root, project, name, run, tool, engine_tag):
    """在獨立目錄產生候選文件，先收集診斷，不覆寫成功產物。"""
    folder = run / name
    tag = folder / (name + ".tag")
    warnings = run / (name + "-warnings.log")
    text = (project / "Doxyfile").read_text(encoding="utf-8-sig")
    text += (f'\nOUTPUT_DIRECTORY = "{run.as_posix()}"\nHTML_OUTPUT = {name}\n'
             f'GENERATE_TAGFILE = "{tag.as_posix()}"\nWARN_LOGFILE = "{warnings.as_posix()}"\n')
    if name == "app":
        text += f'TAGFILES = "{engine_tag.as_posix()}=../libuxplay"\n'
    command = [tool, "-"]
    if tool == "docker":
        text = text.replace(root.as_posix(), "/src")
        work = "/src/libuxplay" if name == "libuxplay" else "/src"
        command = ["docker", "run", "--rm", "-i", "-v", f"{root}:/src", "-w", work,
                   "ubuntu:24.04", "bash", "-lc",
                   "apt-get update -qq >/dev/null && apt-get install -y -qq doxygen >/dev/null && doxygen -"]
    result = subprocess.run(command, input=text.encode("utf-8"), cwd=project,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
    log = result.stdout.decode("utf-8", errors="replace")
    (run / (name + "-build.log")).write_text(log, encoding="utf-8")
    warning_text = warnings.read_text(encoding="utf-8", errors="replace") if warnings.exists() else ""
    if result.returncode or warning_text.strip() or re.search(r"\bwarning:", log, re.IGNORECASE):
        for line in (warning_text or log).splitlines():
            line = re.sub(r"^(.*?):(\d+): (warning|error):", r"\1:\2:1: \3:", line)
            print(line)
        raise ValueError(f"Doxygen failed or warned; previous outputs retained. See {run}")
    if not (folder / "index.html").is_file() or not tag.is_file():
        raise ValueError(f"Missing index/tagfile in candidate: {folder}")
    return folder


class Links(HTMLParser):
    """收集入口頁面的跨站連結。"""
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.links.extend(value for key, value in attrs if key == "href")


def validate_links(sites):
    """依候選或成功的相鄰 API 站檢查入口跨站連結。"""
    for name, directory in sites.items():
        page = directory / "index.html"
        if not page.is_file():
            continue
        parser = Links()
        parser.feed(page.read_text(encoding="utf-8"))
        other = "libuxplay" if name == "app" else "app"
        for link in parser.links:
            prefix = f"../{other}/"
            if link.startswith(prefix) and other in sites:
                target = sites[other] / link[len(prefix):].split("#")[0]
                if not target.is_file():
                    raise ValueError(f"Broken API cross-link: {page}: {link}")


@contextmanager
def build_lock(directory):
    """以作業系統鎖避免同時發佈，程序結束時即會釋放。

    鎖定檔可重用並保留在 build，不需刪除 stale lock；唯讀檢查不建立此檔。
    """
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".build.lock").open("a+b") as handle:
        handle.seek(0, os.SEEK_END)
        if not handle.tell():
            handle.write(b"0")
            handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ValueError("Another API documentation build is already running") from error
        try:
            yield
        finally:
            handle.seek(0)
            if os.name == "nt":
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle, fcntl.LOCK_UN)


def run(args):
    """執行唯讀檢查，或取得寫入鎖後完成建置。"""
    if args.check:
        return coordinate(args)
    with build_lock(ROOT / "build/doxygen"):
        return coordinate(args)


def coordinate(args):
    """協調來源指紋、相依順序、產物驗證與成功發佈。"""
    root = ROOT
    output = root / "build/doxygen"
    state_path = output / "state.json"
    try:
        old = json.loads(state_path.read_text(encoding="utf-8"))
        if old.get("schema") != 1:
            old = {}
    except (OSError, ValueError):
        old = {}
    tool = "docker" if args.docker else args.doxygen or os.environ.get("DOXYGEN") or shutil.which("doxygen")
    if not tool:
        tool = "docker" if shutil.which("docker") else None
    if not tool:
        raise ValueError("Doxygen unavailable; set DOXYGEN or install Doxygen/Docker")
    if tool == "docker" and not shutil.which("docker"):
        raise ValueError("Docker unavailable")
    version = "docker:uncached"
    if tool != "docker":
        version = subprocess.check_output([tool, "--version"], timeout=15).decode("utf-8").strip()
    recipe = digest([Path(__file__).read_bytes().hex(), (root / "scripts/build_docs.ps1").read_bytes().hex()])
    projects = {"libuxplay": root / "libuxplay"}
    if args.target != "Libuxplay":
        projects["app"] = root
    current = dict(old.get("projects", {}))
    sites = {name: output / name for name in projects}
    changes = {}
    declarations = {}
    changed = False
    run_folder = output / "runs" / uuid.uuid4().hex
    for name, project in projects.items():
        files = input_files(project)
        engine_tag = sites["libuxplay"] / "libuxplay.tag"
        dependency = digest(engine_tag.read_bytes()) if name == "app" and engine_tag.is_file() else ""
        fingerprint = digest([files, recipe, version, dependency])
        before = current.get(name, {})
        fresh = before.get("fingerprint") == fingerprint and not args.force and tool != "docker"
        outputs = cached_output_files(output / name, before.get("outputs", {}),
                                      before.get("output_metadata", {})) if fresh else {}
        fresh = (fresh and outputs and before.get("outputs") == outputs
                 and (output / name / "index.html").is_file())
        if name == "libuxplay" and fresh:
            fresh = cached_output_files(project / "build/doxygen/libuxplay", before.get("outputs", {}),
                                        before.get("output_metadata", {})) == outputs
        if args.check and name == "app" and changed:
            fresh = False  # 引擎待更新時，交叉連結的標籤相依也可能改變。
        if args.force or tool == "docker":
            fresh = False
        if fresh:
            print(f"[current] {name}", flush=True)
            continue
        changed = True
        names = sorted(key for key in files.keys() | before.get("inputs", {}).keys()
                       if files.get(key) != before.get("inputs", {}).get(key))
        reason = ", ".join(names[:6]) or "recipe/tool/dependency/output changed or missing"
        if args.check:
            diagnostic(project, "warning", f"API reference outdated: {reason}")
            continue
        print(f"[build] {name}: {reason}", flush=True)
        run_folder.mkdir(parents=True, exist_ok=True)
        candidate = generate(root, project, name, run_folder, tool, engine_tag)
        symbols = api_symbols(candidate / (name + ".tag"))
        changes[name] = api_diff(before.get("symbols", {}), symbols)
        declarations[name] = {
            "added": {key: symbols[key] for key in changes[name]["added"]},
            "removed": {key: before["symbols"][key] for key in changes[name]["removed"]},
            "changed": {key: {"before": before["symbols"][key], "after": symbols[key]}
                        for key in changes[name]["changed"]},
        }
        current[name] = {"fingerprint": fingerprint, "inputs": files,
                         "outputs": tree_files(candidate), "symbols": symbols,
                         "output_metadata": output_metadata(candidate)}
        sites[name] = candidate
    if args.check:
        return 1 if changed else 0
    if not changed:
        return 0
    validate_links(sites)
    replacements = [(sites[name], output / name) for name in changes]
    if "libuxplay" in changes:
        replacements.append((sites["libuxplay"], root / "libuxplay/build/doxygen/libuxplay"))
    # 遞迴搬移或複製前先確認完整目標仍在工作區內。
    publish(replacements, run_folder, root)
    report = ["# API 宣告差異", "", "此報告只比較索引宣告，不是完整 ABI 檢查；首次執行表示建立基線。", ""]
    for name, difference in changes.items():
        counts = ", ".join(f"{key}={len(values)}" for key, values in difference.items())
        print(f"[API] {name}: {counts}")
        report.extend([f"## {name}", "", counts, ""])
        for kind, values in difference.items():
            for value in values:
                report.append(f"- {kind}: `{value}`")
                if kind == "changed":
                    detail = declarations[name][kind][value]
                    report.extend(["", "  原宣告：`" + " / ".join(detail["before"]) + "`",
                                   "", "  新宣告：`" + " / ".join(detail["after"]) + "`", ""])
        report.append("")
    (output / "api-diff.md").write_text("\n".join(report), encoding="utf-8")
    (output / "api-diff.json").write_text(json.dumps(declarations, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary = run_folder / "state.json"
    temporary.write_text(json.dumps({"schema": 1, "tool": version, "projects": current},
                                    ensure_ascii=False, indent=2), encoding="utf-8")
    replace_generated(temporary, state_path)
    print(f"Report: {output / 'api-diff.md'}", flush=True)
    return 0


def main():
    """解析內部 CLI；Windows 公開入口維持 build_docs.ps1。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=str.capitalize, choices=["All", "App", "Libuxplay"], default="All")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--docker", action="store_true")
    parser.add_argument("--doxygen")
    args = parser.parse_args()
    if args.check and args.force:
        parser.error("--check and --force cannot be combined")
    try:
        return run(args)
    except (OSError, ValueError, subprocess.SubprocessError, ET.ParseError) as error:
        diagnostic(ROOT, "error", str(error))
        return 2


if __name__ == "__main__":
    sys.exit(main())

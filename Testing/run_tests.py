"""Build and run native regressions with the existing UCRT64 development toolchain."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def run(command, *, timeout=120):
    """Run one checked build/test command, propagating errors and bounded waits."""
    subprocess.run([str(value) for value in command], check=True, timeout=timeout)


def main():
    """Resolve the configured compiler prefix and execute the regression suite."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, help="UCRT64 prefix containing bin/gcc.exe")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)
    prefix = arguments.prefix
    if prefix is None:
        cache = (root / "build/dev/CMakeCache.txt").read_text(encoding="utf-8")
        compiler = next(line.split("=", 1)[1] for line in cache.splitlines()
                        if line.startswith("CMAKE_C_COMPILER:"))
        prefix = Path(compiler).parent.parent
    tools = prefix / "bin"
    os.environ["PATH"] = str(tools) + os.pathsep + os.environ["PATH"]
    output = root / "build/Testing"
    output.mkdir(parents=True, exist_ok=True)

    def c_test(name, sources, flags=()):
        executable = output / (name + ".exe")
        run([tools / "gcc.exe", *flags, "-Ilibuxplay/lib", *sources,
             "-lws2_32", "-lpthread", "-o", executable])
        run([executable], timeout=45)

    def qt_test(name, sources, modules, libraries=()):
        flags = shlex.split(subprocess.check_output(
            [tools / "pkg-config.exe", "--cflags", "--libs", *modules], text=True))
        executable = output / (name + ".exe")
        run([tools / "g++.exe", "-std=c++17", *sources, *flags, *libraries,
             "-lpthread", "-o", executable])
        run([executable], timeout=45)

    c_test("audio_buffer_test", ["Testing/audio_buffer_test.c", "libuxplay/lib/byteutils.c"],
           ["-Wall", "-Wextra"])
    c_test("audio_delivery_test", ["Testing/audio_delivery_test.c", "libuxplay/lib/byteutils.c"],
           ["-O2", "-flto"])
    for mode in ("rtp", "ntp"):
        flags = ["-O2", "-flto", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
        if mode == "ntp":
            flags.append("-DTEST_NTP")
        c_test(mode + "_cleanup_test", ["Testing/worker_cleanup_test.c"], flags)
    qt_test("gpu_probe_test", ["Testing/gpu_probe_test.cpp"], ["Qt6Core"],
            ["-ld3d11", "-ldxgi", "-ldxguid", "-luuid"])
    qt_test("logging_test", ["Testing/logging_test.cpp", "src/logging.cpp"], ["Qt6Core", "Qt6Gui"])
    qt_test("hud_overlay_test", ["Testing/hud_overlay_test.cpp", "src/hudoverlay.cpp"],
            ["Qt6Core"], ["-luser32", "-lgdi32"])
    qt_test("settings_file_test", ["Testing/settings_file_test.cpp", "src/settingsfile.cpp"], ["Qt6Core"])
    print("All 8 native regression programs passed.")


if __name__ == "__main__":
    main()

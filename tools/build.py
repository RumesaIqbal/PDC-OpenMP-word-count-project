"""Build the three C++ binaries without requiring GNU make."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
EXT = ".exe" if sys.platform == "win32" else ""

SOURCES = {
    "wc_seq": "src/seq_main.cpp",
    "wc_par": "src/par_main.cpp",
    "wc_verify": "src/verify_main.cpp",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="rebuild even if up to date")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)

    flags = ["g++", "-O2", "-std=c++17", "-fopenmp", "-Wall", "-Wextra", "-Iinclude"]
    libs = ["-lpsapi"] if sys.platform == "win32" else []
    headers = list((ROOT / "include").glob("*.hpp"))

    for name, source in SOURCES.items():
        binary = BUILD / f"{name}{EXT}"
        source_path = ROOT / source
        if not args.force and binary.exists():
            newest_header = max((p.stat().st_mtime for p in headers), default=0)
            if binary.stat().st_mtime >= max(source_path.stat().st_mtime, newest_header):
                continue
        print(f"building {binary.name}")
        subprocess.run(flags + [source, "-o", str(binary)] + libs, cwd=ROOT, check=True)
    print("build complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

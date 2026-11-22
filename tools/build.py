"""Build the three C/OpenMP binaries without requiring GNU make."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
EXT = ".exe" if sys.platform == "win32" else ""

SOURCES = {
    "wc_seq": ("src/seq_main.c", "src/wc.c"),
    "wc_par": ("src/par_main.c", "src/wc.c"),
    "wc_verify": ("src/verify_main.c",),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="rebuild even if up to date")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)

    flags = ["gcc", "-O2", "-std=c11", "-fopenmp", "-Wall", "-Wextra", "-Iinclude"]
    libs = ["-lpsapi"] if sys.platform == "win32" else []
    headers = list((ROOT / "include").glob("*.h"))

    for name, sources in SOURCES.items():
        binary = BUILD / f"{name}{EXT}"
        source_paths = [ROOT / source for source in sources]
        if not args.force and binary.exists():
            newest_input = max((p.stat().st_mtime for p in source_paths + headers), default=0)
            if binary.stat().st_mtime >= newest_input:
                continue
        print(f"building {binary.name}")
        subprocess.run(flags + [str(source.relative_to(ROOT)) for source in source_paths] +
                       ["-o", str(binary)] + libs, cwd=ROOT, check=True)
    print("build complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

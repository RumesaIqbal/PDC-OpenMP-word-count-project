"""Build the four benchmark files from the canonical cleaned corpus.

Sizes are produced by repeating the canonical text and stopping at a line
boundary before the target is exceeded. Actual sizes are recorded in the
manifest and are used in all speedup calculations.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CANONICAL = ROOT / "data" / "clean" / "canonical.txt"
DEFAULT_OUTDIR = ROOT / "data" / "datasets"

MIB = 1024 ** 2
GIB = 1024 ** 3
TARGETS: list[tuple[str, int]] = [
    ("500MB", 500 * MIB),
    ("1GB", 1 * GIB),
    ("2GB", 2 * GIB),
    ("5GB", 5 * GIB),
]


def repeated_lines(path: Path):
    """Yield every line of `path`, restarting from the top after EOF."""
    while True:
        with open(path, "rb") as source:
            for line in source:
                yield line


def build_size(canonical: Path, destination: Path, target: int) -> dict:
    started = time.perf_counter()
    digest = hashlib.sha256()
    written = 0
    with open(destination, "wb") as out:
        for line in repeated_lines(canonical):
            if written + len(line) > target:
                if written == 0:  # a single line larger than the target
                    line = line[:target]
                else:
                    break
            out.write(line)
            digest.update(line)
            written += len(line)
            if written >= target:
                break
    return {
        "file": destination.name,
        "target_bytes": target,
        "actual_bytes": written,
        "sha256": digest.hexdigest(),
        "seconds": round(time.perf_counter() - started, 3),
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(8 * MIB), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--canonical", type=Path, default=DEFAULT_CANONICAL)
    parser.add_argument("--outdir", type=Path, default=DEFAULT_OUTDIR)
    parser.add_argument("--only", choices=[name for name, _ in TARGETS], default=None)
    args = parser.parse_args()

    if not args.canonical.exists():
        parser.error(f"canonical corpus not found: {args.canonical}; run preprocess.py first")
    args.outdir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "canonical": {
            "file": str(args.canonical),
            "bytes": args.canonical.stat().st_size,
            "sha256": sha256_file(args.canonical),
        },
        "generation_rule": "repeat canonical text; stop at a line boundary before exceeding target",
        "datasets": [],
    }
    for name, target in TARGETS:
        if args.only is not None and args.only != name:
            continue
        destination = args.outdir / f"{name}.txt"
        entry = build_size(args.canonical, destination, target)
        entry["name"] = name
        manifest["datasets"].append(entry)
        print(f"{name}: {entry['actual_bytes']} bytes -> {destination}")

    manifest_path = args.outdir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

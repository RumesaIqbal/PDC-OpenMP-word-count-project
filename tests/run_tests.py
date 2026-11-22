"""Correctness gate: sequential vs hand-computed expected, parallel vs sequential.

Runs every fixture through wc_seq and through a matrix of wc_par configurations.
All count tables must match byte-for-byte before any benchmark is trusted.
"""

from __future__ import annotations

import itertools
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures"
BUILD = ROOT / "build"
EXT = ".exe" if sys.platform == "win32" else ""

SEQ = BUILD / f"wc_seq{EXT}"
PAR = BUILD / f"wc_par{EXT}"
VERIFY = BUILD / f"wc_verify{EXT}"

THREADS = (1, 2, 4, 8, 16)
SCHEDULES = ("static", "dynamic")
MERGES = ("critical", "tree")
CHUNKS = (None, 16)
BUFFERS = (4 * 1024 * 1024, 8)


def build() -> None:
    BUILD.mkdir(exist_ok=True)
    common = ["g++", "-O2", "-std=c++17", "-fopenmp", "-Wall", "-Wextra", "-Iinclude"]
    libs = ["-lpsapi"] if sys.platform == "win32" else []
    sources = {
        SEQ: "src/seq_main.cpp",
        PAR: "src/par_main.cpp",
        VERIFY: "src/verify_main.cpp",
    }
    for binary, source in sources.items():
        if not binary.exists():
            subprocess.run(common + [source, "-o", str(binary)] + libs,
                           cwd=ROOT, check=True)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True)


def main() -> int:
    build()
    failures: list[str] = []
    total = 0
    fixtures = sorted(FIXTURES.glob("*.txt"))

    with tempfile.TemporaryDirectory(prefix="pdc-wc-tests-") as tmp:
        tmpdir = Path(tmp)
        for fixture in fixtures:
            expected = FIXTURES / f"{fixture.stem}.expected.tsv"
            seq_out = tmpdir / f"{fixture.stem}.seq.tsv"

            result = run([str(SEQ), str(fixture), "--top", "5", "--out", str(seq_out)])
            total += 1
            if result.returncode != 0:
                failures.append(f"{fixture.name}: wc_seq failed: {result.stderr.strip()}")
                continue
            if seq_out.read_bytes() != expected.read_bytes():
                failures.append(f"{fixture.name}: sequential output != {expected.name}")
            verified = run([str(VERIFY), str(seq_out), str(expected)])
            total += 1
            if verified.returncode != 0 or "MATCH" not in verified.stdout:
                failures.append(f"{fixture.name}: wc_verify did not report MATCH")

            configs = itertools.product(THREADS, SCHEDULES, MERGES, CHUNKS, BUFFERS)
            for threads, schedule, merge, chunks, buffer_bytes in configs:
                par_out = tmpdir / "par.tsv"
                command = [str(PAR), str(fixture), "--threads", str(threads),
                           "--schedule", schedule, "--merge", merge,
                           "--buffer-bytes", str(buffer_bytes), "--out", str(par_out)]
                if chunks is not None:
                    command += ["--num-chunks", str(chunks)]
                result = run(command)
                total += 1
                if result.returncode != 0:
                    failures.append(f"{fixture.name} t={threads} {schedule}/{merge} "
                                    f"k={chunks} b={buffer_bytes}: failed: "
                                    f"{result.stderr.strip()}")
                    continue
                if par_out.read_bytes() != seq_out.read_bytes():
                    failures.append(f"{fixture.name} t={threads} {schedule}/{merge} "
                                    f"k={chunks} b={buffer_bytes}: output differs")

    print(f"ran {total} checks across {len(fixtures)} fixtures")
    if failures:
        print(f"FAILED: {len(failures)}")
        for failure in failures[:30]:
            print("  -", failure)
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

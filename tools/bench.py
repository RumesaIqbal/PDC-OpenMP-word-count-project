"""Run the full benchmark matrix and produce CSV + plots.

Example (smoke test):
    py tools/bench.py --sizes 500MB --threads 1 2 4 --reps 2 --warmup 1

Full run (hours on 5 GB):
    py tools/bench.py
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import os
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
EXT = ".exe" if sys.platform == "win32" else ""
SEQ = BUILD / f"wc_seq{EXT}"
PAR = BUILD / f"wc_par{EXT}"
VERIFY = BUILD / f"wc_verify{EXT}"
DATASETS = ROOT / "data" / "datasets"
RESULTS = ROOT / "results"

FIELDS = [
    "dataset", "label", "impl", "threads", "schedule", "merge", "num_chunks", "rep",
    "t_setup", "t_parallel", "t_read", "t_count", "t_merge_wait", "t_merge_hold",
    "t_topn", "t_total", "speedup", "efficiency", "peak_rss_kb", "total_tokens",
    "unique_words", "match", "size_bytes",
]


def discover_datasets() -> list[tuple[str, Path, int]]:
    manifest = DATASETS / "manifest.json"
    if manifest.exists():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        return [
            (entry["name"], DATASETS / entry["file"], entry["actual_bytes"])
            for entry in data["datasets"]
        ]
    return [
        (path.stem, path, path.stat().st_size)
        for path in sorted(DATASETS.glob("*.txt"))
    ]


def run_once(binary: Path, dataset: Path, extra: list[str],
             counts: Path, stats_path: Path) -> dict:
    command = [str(binary), str(dataset), "--out", str(counts),
               "--stats", str(stats_path)] + extra
    env = os.environ.copy()
    env["OMP_PROC_BIND"] = "TRUE"
    env["OMP_PLACES"] = "cores"
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{binary.name} failed: {result.stderr.strip()}")
    return json.loads(stats_path.read_text(encoding="utf-8"))


def median(values: list[float]) -> float:
    return statistics.median(values)


def make_row(label: str, dataset: Path, size: int, impl: str, threads: int,
             schedule: str, merge: str, stats: dict, rep: int, match: bool,
             baseline: float) -> dict:
    total = stats["t_total"]
    speedup = baseline / total if impl == "parallel" and total > 0 else 1.0
    return {
        "dataset": dataset.name, "label": label, "impl": impl, "threads": threads,
        "schedule": schedule, "merge": merge,
        "num_chunks": stats.get("num_chunks", 0), "rep": rep,
        "t_setup": stats["t_setup"], "t_parallel": stats["t_parallel"],
        "t_read": stats["t_read"], "t_count": stats["t_count"],
        "t_merge_wait": stats["t_merge_wait"], "t_merge_hold": stats["t_merge_hold"],
        "t_topn": stats["t_topn"], "t_total": total,
        "speedup": speedup, "efficiency": speedup / threads if impl == "parallel" else 1.0,
        "peak_rss_kb": stats["peak_rss_kb"], "total_tokens": stats["total_tokens"],
        "unique_words": stats["unique_words"], "match": int(match),
        "size_bytes": size,
    }


def write_csv(path: Path, rows: list[dict]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def summarise(rows: list[dict]) -> list[dict]:
    groups: dict[tuple, list[dict]] = {}
    for row in rows:
        key = (row["label"], row["impl"], row["threads"], row["schedule"], row["merge"])
        groups.setdefault(key, []).append(row)
    summary = []
    for key, group in groups.items():
        summary.append({
            "label": key[0], "impl": key[1], "threads": key[2],
            "schedule": key[3], "merge": key[4],
            "t_total": median([r["t_total"] for r in group]),
            "t_setup": median([r["t_setup"] for r in group]),
            "t_read": median([r["t_read"] for r in group]),
            "t_count": median([r["t_count"] for r in group]),
            "t_merge_wait": median([r["t_merge_wait"] for r in group]),
            "t_merge_hold": median([r["t_merge_hold"] for r in group]),
            "t_topn": median([r["t_topn"] for r in group]),
            "speedup": median([r["speedup"] for r in group]),
            "efficiency": median([r["efficiency"] for r in group]),
            "peak_rss_kb": median([r["peak_rss_kb"] for r in group]),
            "matches_all": all(r["match"] for r in group),
            "runs": len(group),
        })
    return sorted(summary, key=lambda r: (r["label"], r["impl"], r["threads"],
                                          r["schedule"], r["merge"]))


def make_plots(summary: list[dict], outdir: Path) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available; skipping plots")
        return

    labels = sorted({r["label"] for r in summary})
    primary = [r for r in summary if r["impl"] == "parallel" and
               r["schedule"] == "static" and r["merge"] == "critical"]

    fig, ax = plt.subplots(figsize=(7, 5))
    for label in labels:
        points = sorted([r for r in primary if r["label"] == label],
                        key=lambda r: r["threads"])
        if points:
            ax.plot([r["threads"] for r in points], [r["speedup"] for r in points],
                    marker="o", label=label)
    max_thread = max((r["threads"] for r in primary), default=1)
    ax.plot([1, max_thread], [1, max_thread], "--", color="gray", label="ideal")
    ax.set(xlabel="threads", ylabel="speedup", title="Speedup vs threads (static/critical)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "speedup.png", dpi=140)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    for label in labels:
        points = sorted([r for r in primary if r["label"] == label],
                        key=lambda r: r["threads"])
        if points:
            ax.plot([r["threads"] for r in points], [r["efficiency"] for r in points],
                    marker="o", label=label)
    ax.axhline(1.0, ls="--", color="gray")
    ax.set(xlabel="threads", ylabel="efficiency", title="Parallel efficiency")
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "efficiency.png", dpi=140)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    for label in labels:
        points = sorted([r for r in primary if r["label"] == label],
                        key=lambda r: r["threads"])
        if points:
            ax.plot([r["threads"] for r in points],
                    [r["peak_rss_kb"] / 1024 for r in points], marker="o", label=label)
    ax.set(xlabel="threads", ylabel="peak RSS (MB)", title="Memory vs threads")
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "memory.png", dpi=140)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    for label in labels:
        points = sorted([r for r in summary if r["label"] == label and
                         r["impl"] == "parallel" and r["schedule"] == "static" and
                         r["merge"] == "critical"], key=lambda r: r["threads"])
        if points:
            ax.plot([r["threads"] for r in points],
                    [(r["t_merge_wait"] + r["t_merge_hold"]) / r["t_total"] * 100
                     for r in points], marker="o", label=label)
    ax.set(xlabel="threads", ylabel="merge time (%)",
           title="Synchronization overhead (critical merge)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "merge_overhead.png", dpi=140)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", nargs="+", default=None,
                        help="dataset labels from manifest.json (default: all)")
    parser.add_argument("--threads", nargs="+", type=int, default=[1, 2, 4, 8, 16])
    parser.add_argument("--schedules", nargs="+", default=["static", "dynamic"])
    parser.add_argument("--merges", nargs="+", default=["critical", "tree"])
    parser.add_argument("--reps", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--outdir", type=Path, default=RESULTS)
    args = parser.parse_args()

    if args.reps < 1 or args.warmup < 0:
        parser.error("--reps must be >= 1 and --warmup >= 0")
    for binary in (SEQ, PAR):
        if not binary.exists():
            parser.error(f"missing {binary}; build the binaries first")

    datasets = discover_datasets()
    if args.sizes:
        datasets = [entry for entry in datasets if entry[0] in args.sizes]
    if not datasets:
        parser.error("no datasets found; run tools/make_sizes.py first")

    args.outdir.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []

    with tempfile.TemporaryDirectory(prefix="pdc-wc-bench-") as tmp:
        tmpdir = Path(tmp)
        counts = tmpdir / "counts.tsv"
        stats_path = tmpdir / "stats.json"
        seq_counts = tmpdir / "seq_counts.tsv"

        for label, dataset, size in datasets:
            print(f"[{label}] sequential baseline ...", flush=True)
            seq_stats = []
            for rep in range(args.warmup + args.reps):
                stats = run_once(SEQ, dataset, [], counts, stats_path)
                if rep == args.warmup:
                    seq_counts.write_bytes(counts.read_bytes())
                seq_stats.append(stats)
            baseline = median([s["t_total"] for s in seq_stats[args.warmup:]])
            for rep, stats in enumerate(seq_stats[args.warmup:], start=1):
                rows.append(make_row(label, dataset, size, "sequential", 1, "none",
                                     "none", stats, rep, True, baseline))

            configs = itertools.product(args.threads, args.schedules, args.merges)
            for threads, schedule, merge in configs:
                print(f"[{label}] t={threads} {schedule}/{merge} ...", flush=True)
                extra = ["--threads", str(threads), "--schedule", schedule,
                         "--merge", merge]
                for rep in range(args.warmup + args.reps):
                    stats = run_once(PAR, dataset, extra, counts, stats_path)
                    match = True
                    if rep == args.warmup:
                        match = counts.read_bytes() == seq_counts.read_bytes()
                        if not match:
                            verify = subprocess.run(
                                [str(VERIFY), str(seq_counts), str(counts)],
                                cwd=ROOT, capture_output=True, text=True)
                            print(verify.stdout[-2000:])
                    if rep >= args.warmup:
                        rows.append(make_row(label, dataset, size, "parallel", threads,
                                             schedule, merge, stats, rep - args.warmup,
                                             match, baseline))

    write_csv(args.outdir / "raw_runs.csv", rows)
    summary = summarise(rows)
    if summary:
        with open(args.outdir / "summary.csv", "w", newline="", encoding="utf-8") as out:
            writer = csv.DictWriter(out, fieldnames=list(summary[0].keys()))
            writer.writeheader()
            writer.writerows(summary)
    make_plots(summary, args.outdir)

    bad = [row for row in summary if not row["matches_all"]]
    print(f"wrote {len(rows)} runs and {len(summary)} configurations to {args.outdir}")
    print(f"correctness failures: {len(bad)}")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())

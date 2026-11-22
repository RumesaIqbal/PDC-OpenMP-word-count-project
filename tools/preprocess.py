"""Stream one or more Wikipedia XML dumps into a canonical plain-text corpus.

Usage:
  py tools/preprocess.py                         # all data/raw/enwiki_parts/*.bz2
  py tools/preprocess.py --dump part1.bz2 part2.bz2
  py tools/preprocess.py --dump extra.bz2 --append

Preprocessing is timed separately and is NOT included in the word-count
speedup calculation. The output has one cleaned article per line.
"""

from __future__ import annotations

import argparse
import bz2
import glob
import hashlib
import json
import re
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DUMP_GLOB = ROOT / "data" / "raw" / "enwiki_parts" / "*.bz2"
DEFAULT_OUT = ROOT / "data" / "clean" / "canonical.txt"

# Wiki markup removal is deliberately simple and documented in the report:
# one level of templates, simple links, refs, comments and residual HTML tags.
MARKUP: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"<!--.*?-->", re.S), " "),
    (re.compile(r"<ref\b[^>]*>.*?</ref>", re.S | re.I), " "),
    (re.compile(r"<ref\b[^>]*/>", re.I), " "),
    (re.compile(r"<[^>]+>", re.S), " "),
    (re.compile(r"\{\{[^{}]*\}\}"), " "),
    (re.compile(r"\[\[(?:[^\]|]+\|)?([^\]]+)\]\]"), r"\1"),
    (re.compile(r"''+"), " "),
    (re.compile(r"\s+"), " "),
]


def local_name(tag: str) -> str:
    return tag.rpartition("}")[2]


def child(parent: ET.Element | None, name: str) -> ET.Element | None:
    if parent is None:
        return None
    for candidate in parent:
        if local_name(candidate.tag) == name:
            return candidate
    return None


def element_text(element: ET.Element | None) -> str:
    if element is None:
        return ""
    return element.text or ""


def clean(text: str) -> str:
    for pattern, replacement in MARKUP:
        text = pattern.sub(replacement, text)
    return text.strip()


def expand_dumps(patterns: list[str]) -> list[Path]:
    found: set[Path] = set()
    for pattern in patterns:
        for match in glob.glob(pattern):
            found.add(Path(match))
    return sorted(found)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def preprocess(dumps: list[Path], out_path: Path, append: bool = False,
               limit_pages: int | None = None) -> dict:
    started = time.perf_counter()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    written = out_path.stat().st_size if append and out_path.exists() else 0
    if append and out_path.exists():
        with open(out_path, "rb") as existing:
            for block in iter(lambda: existing.read(8 * 1024 * 1024), b""):
                digest.update(block)

    pages = kept = skipped = 0
    part_stats: list[dict] = []

    with open(out_path, "ab" if append else "wb") as out:
        for dump in dumps:
            part_started = time.perf_counter()
            part_pages = part_kept = part_skipped = part_bytes = 0
            with bz2.open(dump, "rb") as raw:
                context = ET.iterparse(raw, events=("end",))
                for _, element in context:
                    if local_name(element.tag) != "page":
                        continue
                    pages += 1
                    part_pages += 1
                    ns_text = element_text(child(element, "ns")).strip()
                    has_redirect = child(element, "redirect") is not None
                    text_element = child(child(element, "revision"), "text")
                    content = element_text(text_element)

                    if ns_text == "0" and not has_redirect and content.strip():
                        cleaned = clean(content)
                        if cleaned:
                            line = (cleaned + "\n").encode("utf-8")
                            out.write(line)
                            digest.update(line)
                            written += len(line)
                            part_bytes += len(line)
                            kept += 1
                            part_kept += 1
                        else:
                            skipped += 1
                            part_skipped += 1
                    else:
                        skipped += 1
                        part_skipped += 1

                    # ElementTree (unlike lxml) has no parent pointers;
                    # clearing each finished page keeps the tree bounded.
                    element.clear()

                    if limit_pages is not None and part_pages >= limit_pages:
                        break

            part_stats.append({
                "dump": str(dump),
                "bytes": dump.stat().st_size,
                "pages_seen": part_pages,
                "articles_written": part_kept,
                "pages_skipped": part_skipped,
                "text_bytes": part_bytes,
                "seconds": round(time.perf_counter() - part_started, 3),
            })
            print(f"finished {dump.name}: {part_kept} articles, "
                  f"{part_bytes / 1024**2:.1f} MB text", flush=True)

    stats_path = out_path.parent / "preprocess_stats.json"
    if append and stats_path.exists():
        previous = json.loads(stats_path.read_text(encoding="utf-8"))
        part_stats = previous.get("dumps", []) + part_stats
        pages += previous.get("pages_seen", 0)
        kept += previous.get("articles_written", 0)
        skipped += previous.get("pages_skipped", 0)

    stats = {
        "source": "English Wikipedia (enwiki) pages-articles parts",
        "dumps": part_stats,
        "output_file": str(out_path),
        "output_bytes": written,
        "output_sha256": digest.hexdigest(),
        "pages_seen": pages,
        "articles_written": kept,
        "pages_skipped": skipped,
        "seconds": round(time.perf_counter() - started, 3),
    }
    stats_path.write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", nargs="+", default=None,
                        help="dump paths or glob patterns (default: data/raw/enwiki_parts/*.bz2)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--append", action="store_true", help="append to an existing corpus")
    parser.add_argument("--limit-pages", type=int, default=None,
                        help="process at most N pages per dump (for quick tests)")
    args = parser.parse_args()

    patterns = args.dump or [str(DEFAULT_DUMP_GLOB)]
    dumps = expand_dumps(patterns)
    if not dumps:
        parser.error(f"no dump files matched: {patterns}")
    stats = preprocess(dumps, args.out, args.append, args.limit_pages)
    print(json.dumps(stats, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

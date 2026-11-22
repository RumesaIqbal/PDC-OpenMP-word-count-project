"""Stream a Wikipedia XML dump into one canonical plain-text corpus.

Usage:  py tools/preprocess.py [--dump data/raw/....bz2] [--out data/clean/canonical.txt]

Preprocessing is timed separately and is NOT included in the word-count
speedup calculation. The output has one cleaned article per line.
"""

from __future__ import annotations

import argparse
import bz2
import hashlib
import json
import re
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DUMP = ROOT / "data" / "raw" / "simplewiki-latest-pages-articles.xml.bz2"
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


def preprocess(dump: Path, out_path: Path, limit_pages: int | None = None) -> dict:
    started = time.perf_counter()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    pages = kept = skipped = 0
    written = 0

    with bz2.open(dump, "rb") as raw, open(out_path, "wb") as out:
        context = ET.iterparse(raw, events=("end",))
        for _, element in context:
            if local_name(element.tag) != "page":
                continue
            pages += 1
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
                    kept += 1
                else:
                    skipped += 1
            else:
                skipped += 1

            # ElementTree (unlike lxml) has no parent pointers; clearing each
            # finished page keeps the in-memory tree bounded.
            element.clear()

            if limit_pages is not None and pages >= limit_pages:
                break

    stats = {
        "source_dump": str(dump),
        "source_bytes": dump.stat().st_size,
        "output_file": str(out_path),
        "output_bytes": written,
        "output_sha256": digest.hexdigest(),
        "pages_seen": pages,
        "articles_written": kept,
        "pages_skipped": skipped,
        "seconds": round(time.perf_counter() - started, 3),
    }
    stats_path = out_path.parent / "preprocess_stats.json"
    stats_path.write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, default=DEFAULT_DUMP)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--limit-pages", type=int, default=None,
                        help="only process the first N pages (for quick tests)")
    args = parser.parse_args()
    stats = preprocess(args.dump, args.out, args.limit_pages)
    print(json.dumps(stats, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

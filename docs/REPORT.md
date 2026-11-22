# Parallel Word Count and Large-Scale Text Processing Using OpenMP

CS3006 — Parallel and Distributed Computing, Fall 2026
FAST-NUCES Karachi

Rumesa Iqbal (23K-0734) · Daniya Javaid (23K-0552) · Layqa Fatima (23K-0572)

## 1. Objective

Build a word-frequency system that processes multi-gigabyte text with
shared-memory parallelism, quantify the speedup over a sequential baseline,
and explain where the speedup is limited.

## 2. Dataset and preprocessing

The source is the English Wikipedia (`enwiki`) "pages-articles" dump, Nov 2026
snapshot. Three of the 73 available parts are used (the full 24.4 GB dump does
not fit on this machine): `pages-articles9`, `pages-articles10` and
`pages-articles11`, about 1.7 GB compressed in total. `tools/preprocess.py`
streams the BZ2 files through `xml.etree.ElementTree.iterparse`, keeps
namespace-0 non-redirect articles, strips comments, refs, HTML tags and simple
wiki markup, and writes one cleaned article per line to a canonical corpus.
Preprocessing is timed separately and is excluded from all speedup figures.

Exact page/article counts, elapsed time and the corpus hash are recorded in
`data/clean/preprocess_stats.json`.

`tools/make_sizes.py` repeats the canonical corpus at line boundaries to build
the four test files. Their actual byte sizes and SHA-256 hashes are recorded
in `data/datasets/manifest.json` and are used in all speedup calculations.

## 3. Implementation

### Sequential baseline

`wc_seq` reads the file in 4 MiB buffers, tokenizes with the shared
`include/tokenizer.hpp`, and counts into one `unordered_map`. A partial token
at a buffer edge is carried into the next buffer. It produces the reference
count table and reference timing.

### Normalization

A word is a maximal run of `[A-Za-z0-9']` bytes, lowercased. All other bytes —
including every byte of a non-ASCII UTF-8 sequence — are delimiters. Empty
tokens are discarded. Both binaries call the same tokenizer so their outputs
are directly comparable.

### Chunking

`compute_boundaries()` divides the file into byte ranges and moves each
interior boundary forward to the next delimiter byte. A chunk therefore
starts at a delimiter and ends immediately before one, so no word is split or
counted twice. Each thread opens its own file stream and seeks to its chunk.

### Parallel counting and merging

`wc_par` creates one private `unordered_map` per thread inside the OpenMP
parallel region and counts with no locks. Two schedules are supported:
`static` with one chunk per thread, and `dynamic` with eight chunks per
thread. Merging is implemented in two ways:

- **critical** — each thread adds its local map into the global map inside
  `#pragma omp critical`; wait time (before acquiring the lock) and hold time
  (inside the lock) are measured separately;
- **tree** — after the parallel region, local maps are pairwise merged in
  `log2(T)` rounds using `#pragma omp parallel for`.

The global table is sorted by `(count desc, word asc)` and the full table is
written as TSV; `--top N` only controls console output.

## 4. Correctness methodology

`tests/run_tests.py` checks six fixtures (empty file, no trailing newline,
mixed case/punctuation, UTF-8, a token crossing a tiny read buffer, and long
tokens that force boundary snapping) against hand-computed expected tables,
then runs 80 parallel configurations per fixture and compares each output
byte-for-byte with the sequential output. 492 checks pass. `wc_verify` also
reports a digest and the first differing rows.

## 5. Performance methodology

For each dataset the sequential run is repeated `1 + 5` times (one warm-up,
five measured); each parallel configuration is repeated the same way. The
machine has 4 physical / 8 logical cores (i7-1185G7), 32 GB RAM and an SSD.
`OMP_PROC_BIND=TRUE` and `OMP_PLACES=cores` are set. Reported metrics are
median wall time, speedup `T_seq / T_par`, efficiency `speedup / threads`,
peak RSS, merge wait/hold time, and a time breakdown (setup, read, count,
merge, top-N). 16 threads is a labelled oversubscription experiment.

## 6. Results

_To be filled from `results/summary.csv` after the full benchmark run._

| Dataset | T_seq (s) | T_par@8 (s) | Speedup@8 | Efficiency@8 | Peak RSS@8 (MB) |
|---|---:|---:|---:|---:|---:|
| 500 MB | — | — | — | — | — |
| 1 GB | — | — | — | — | — |
| 2 GB | — | — | — | — | — |
| 5 GB | — | — | — | — | — |

_To be filled: static vs dynamic, critical vs tree, merge overhead percentage._

## 7. Discussion

Expected observations to verify with the measurements:

- Counting is CPU-bound only while data is in cache; reading a multi-gigabyte
  file can limit speedup, which is why read time is reported separately.
- Thread-local maps avoid contention during counting; the critical-section
  merge is serial, while tree reduction shortens the merge at high thread
  counts at the cost of extra memory movement.
- 16 threads on 8 logical cores is expected to show flat or falling
  efficiency.

## 8. Division of work

| Member | Features | Contribution |
|---|---|---|
| Rumesa Iqbal | F1, F2, F6 | Preprocessing pipeline and sequential baseline |
| Daniya Javaid | F3, F4, F5 | Boundary computation, OpenMP counting, thread-local tables |
| Layqa Fatima | F7, F8 | Merging, verification, benchmarks, plots and report |

## 9. References

1. OpenMP Architecture Review Board, *OpenMP Specifications* — https://www.openmp.org/specifications/
2. GCC libgomp documentation — https://gcc.gnu.org/onlinedocs/libgomp/
3. Wikimedia Foundation, *Wikimedia Dumps* — https://dumps.wikimedia.org/

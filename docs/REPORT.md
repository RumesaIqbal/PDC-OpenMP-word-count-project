# Parallel Word Count and Large-Scale Text Processing Using OpenMP

CS3006 — Parallel and Distributed Computing, Fall 2026
FAST-NUCES Karachi

Rumesa Iqbal (23K-0734) · Daniya Javaid (23K-0552) · Layqa Fatima (23K-0572)

## 1. Objective

Build a word-frequency system that processes multi-gigabyte text with
shared-memory parallelism, quantify the speedup over a sequential baseline,
and explain where the speedup is limited.

## 2. Dataset and preprocessing

The source is the `simplewiki-latest-pages-articles.xml.bz2` Wikipedia dump
(Nov 2026 snapshot, 360,149,050 bytes compressed). `tools/preprocess.py`
streams the BZ2 file through `xml.etree.ElementTree.iterparse`, keeps
namespace-0 non-redirect articles, strips comments, refs, HTML tags and simple
wiki markup, and writes one cleaned article per line to a canonical corpus.
Preprocessing is timed separately and is excluded from all speedup figures.

The run processed 563,515 pages, wrote 286,107 articles and skipped 277,408
non-article/redirect/empty pages in 92.193 seconds, producing a 518,124,586-byte
canonical corpus (SHA-256 `2343375d…d5fe2d9`).

`tools/make_sizes.py` repeats the canonical corpus at line boundaries to build
the four test files. Their actual byte sizes are 524,287,917 (500 MB),
1,073,739,121 (1 GB), 2,147,483,561 (2 GB) and 5,368,707,908 (5 GB); exact
SHA-256 hashes are recorded in `data/datasets/manifest.json`.

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

For each dataset the sequential run is repeated `1 + 3` times (one warm-up,
three measured); each parallel configuration is repeated the same way. The
machine has 4 physical / 8 logical cores (i7-1185G7), 32 GB RAM and an SSD.
`OMP_PROC_BIND=TRUE` and `OMP_PLACES=cores` are set. Reported metrics are
median wall time, speedup `T_seq / T_par`, efficiency `speedup / threads`,
peak RSS, merge wait/hold time, and a time breakdown (setup, read, count,
merge, top-N). 16 threads is a labelled oversubscription experiment.

## 6. Results

The final benchmark slice measured only `static` scheduling with the `critical`
merge, using 1, 2, 4, 8 and 16 threads. Dynamic scheduling and tree merging
were not re-run in this final slice. Each summary value below is the median of
three measured runs. All 60 parallel measurements matched their sequential
reference output byte-for-byte (`match=1`); the summary reports
`matches_all=True` for all configurations.

| Dataset | T_seq (s) | T_par@8 (s) | Speedup@8 | Efficiency@8 | Peak RSS@8 (MB) |
|---|---:|---:|---:|---:|---:|
| 500 MB | 6.777 | 2.910 | 2.329 | 0.291 | 318 |
| 1 GB | 13.071 | 5.444 | 2.401 | 0.300 | 429 |
| 2 GB | 25.878 | 10.047 | 2.576 | 0.322 | 608 |
| 5 GB | 64.580 | 19.136 | 3.375 | 0.422 | 894 |

Peak RSS is converted from the summary's KiB values using 1,024 KiB per MiB
and rounded to the nearest MB for the table. The summary reports aggregate
merge wait and hold measurements across threads; these values can exceed
wall-clock time and are not interpreted as a percentage of total time.

## 7. Discussion

The eight-thread speedup rises from 2.329 on 500 MB to 3.375 on 5 GB, while
efficiency rises from 0.291 to 0.422. The largest input benefits more from
parallel counting, but the eight-thread run also uses more memory: peak RSS
increases from about 318 MB to 894 MB. At 16 threads, speedup is lower than at
eight threads for every input (about 1.98–2.93 versus 2.33–3.37), with
efficiency between 0.124 and 0.183; this is consistent with diminishing
returns beyond the machine's eight logical cores. The aggregate merge wait
and hold measurements are reported in `results/summary.csv`; because they
sum time across threads, they should not be read as a share of wall-clock
time. These results describe only static/critical; this run does not support
a comparison with dynamic scheduling or tree merging.

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

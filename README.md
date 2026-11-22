# Parallel Word Count (CS3006 PDC Project)

Large-scale word-frequency counting over Wikipedia-derived text using OpenMP,
compared against a sequential baseline on the same cleaned input.

Group members: Rumesa Iqbal (23K-0734), Daniya Javaid (23K-0552),
Layqa Fatima (23K-0572).

## Status

- [x] Repository initialized (the proposal PDF is intentionally **not** tracked)
- [x] Shared C++ core: tokenizer, chunked reader, timing/memory helpers
- [x] Sequential baseline (`wc_seq`)
- [x] OpenMP parallel implementation (`wc_par`)
- [x] Verification harness and fixtures (492 checks pass)
- [x] Dataset preprocessing / sizing scripts
- [x] Benchmark runner and plots
- [ ] Full benchmark run and report write-up

## Build

Requires a C++17 compiler with OpenMP (tested with MSYS2 UCRT64 GCC 16.2).
On Windows, `C:\msys64\ucrt64\bin` must be on `PATH`.

```sh
py tools/build.py            # builds build/wc_seq, wc_par, wc_verify
py tools/build.py --force    # rebuild everything
py tests/run_tests.py        # 486 correctness checks
```

`make all` / `make test` do the same if GNU make is installed.

## Usage

```
wc_seq <file> [--top N] [--out FILE] [--stats FILE] [--buffer-bytes N]

wc_par <file> [--threads T] [--schedule static|dynamic] [--num-chunks K]
             [--merge critical|tree] [--top N] [--out FILE] [--stats FILE]
             [--buffer-bytes N]

wc_verify <a.tsv> <b.tsv> [--max-diff N]
```

`--out` writes the **full** sorted `(word, count)` table as TSV so the two
implementations can be compared byte-for-byte. `--top` controls only the rows
printed to stdout. `--stats` writes a JSON summary with timings, token counts
and peak RSS.

### Parallel design

- Byte offsets are moved forward to the next delimiter before counting, so no
  word is split between chunks.
- Each OpenMP thread owns a private `unordered_map` and streams its chunk(s)
  through a bounded read buffer (default 4 MiB); no lock during counting.
- Merging is available in two modes: `#pragma omp critical` (baseline) and a
  pairwise tree reduction over the local maps. The critical mode records
  separate wait and hold times.
- Defaults: 1 chunk per thread for `static`, 8 chunks per thread for
  `dynamic`; `--num-chunks` overrides both.

## Dataset pipeline

Three `enwiki` pages-articles parts are downloaded into `data/raw/enwiki_parts/`
(ignored by git); the full 73-part dump would not fit on this disk:

```
curl -L -o data/raw/enwiki_parts/<part>.bz2 \
  https://dumps.wikimedia.org/enwiki/latest/<part>.bz2
py tools/preprocess.py                 # combines all parts -> data/clean/canonical.txt
py tools/make_sizes.py                 # -> data/datasets/{500MB,1GB,2GB,5GB}.txt
```

`preprocess.py --dump <part> --append` can add further parts to an existing
corpus if more text is needed.

The four benchmark files repeat the canonical text at line boundaries; their
actual byte sizes and hashes are recorded in `data/datasets/manifest.json`.
Preprocessing is timed separately and excluded from speedup.

## Benchmarks

```
py tools/bench.py --sizes 500MB --threads 1 2 4 --reps 2 --warmup 1   # smoke
py tools/bench.py                                                     # full matrix
```

For every dataset × threads × schedule × merge configuration the runner does
one warm-up plus 5 measured runs, compares the first parallel output against
the sequential output, and writes:

- `results/raw_runs.csv` — every measured run,
- `results/summary.csv` — medians, speedup, efficiency, memory,
- `results/{speedup,efficiency,memory,merge_overhead}.png`.

Speedup is `T_seq / T_par` using the median sequential time for the same
file. Threads 1/2/4/8 are the hardware-scaling points on this 4-core/8-thread
laptop; 16 threads is an explicitly labelled oversubscription experiment.

## Normalization contract

Words are ASCII `[A-Za-z0-9']`, lowercased. Every other byte (including all
non-ASCII UTF-8 bytes) is a delimiter. Both implementations use the same
`include/tokenizer.hpp`, so their outputs are directly comparable.

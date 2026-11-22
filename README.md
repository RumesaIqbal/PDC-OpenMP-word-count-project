# Parallel Word Count (CS3006 PDC Project)

Shared-memory word-frequency counting over large Wikipedia-derived text with
OpenMP, compared against a sequential baseline.

## Current status

- [x] Repository initialized (proposal PDF is intentionally **not** tracked)
- [x] Shared C++ core: tokenizer, chunked file reader, timing/memory helpers
- [x] Sequential baseline (`wc_seq`)
- [x] OpenMP parallel implementation (`wc_par`)
- [x] Verification harness and fixtures (486 checks pass)
- [x] Dataset preprocessing / sizing scripts
- [ ] Benchmark runner and plots

## Build

Requires a C++17 compiler with OpenMP (tested with MSYS2 UCRT64 GCC 16.2).
On Windows, `C:\msys64\ucrt64\bin` must be on `PATH`.

```sh
g++ -O2 -std=c++17 -fopenmp -Wall -Wextra -Iinclude src/seq_main.cpp -o build/wc_seq.exe -lpsapi
g++ -O2 -std=c++17 -fopenmp -Wall -Wextra -Iinclude src/par_main.cpp -o build/wc_par.exe -lpsapi
g++ -O2 -std=c++17 -fopenmp -Wall -Wextra -Iinclude src/verify_main.cpp -o build/wc_verify.exe -lpsapi
```

`make all` does the same if GNU make is installed.

## Usage

```
wc_seq <file> [--top N] [--out FILE] [--stats FILE] [--buffer-bytes N]
```

`--out` writes the **full** sorted `(word, count)` table as TSV; `--top`
controls how many rows are printed to stdout. `--stats` writes a JSON summary
with timings, token counts and peak RSS.

## Dataset pipeline

The proposal PDF is deliberately not tracked. The dump is downloaded into
`data/raw/` (ignored by git):

```
curl -L -o data/raw/simplewiki-latest-pages-articles.xml.bz2 \
  https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
py tools/preprocess.py                 # -> data/clean/canonical.txt
py tools/make_sizes.py                 # -> data/datasets/{500MB,1GB,2GB,5GB}.txt
```

The four benchmark files repeat the canonical text at line boundaries; their
actual byte sizes and hashes are recorded in `data/datasets/manifest.json`.
Preprocessing time is measured separately and excluded from speedup.

## Normalization contract

Words are ASCII `[A-Za-z0-9']`, lowercased. Every other byte (including all
non-ASCII UTF-8 bytes) is a delimiter. The same `include/tokenizer.hpp` is
used by both implementations so their outputs are directly comparable.

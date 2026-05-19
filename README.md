<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  A fast, single-pass URL structural de-duplicator that doesn't throw away your attack surface.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue.svg"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-~6.6MB%20constant-success.svg">
  <a href="https://github.com/ayodyadsr/udud-benchmark"><img src="https://img.shields.io/badge/benchmark-results-orange.svg"></a>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#installation">Installation</a> •
  <a href="#usage">Usage</a> •
  <a href="#examples">Examples</a> •
  <a href="#benchmark">Benchmark</a> •
  <a href="#how-it-works">How it works</a>
</p>

---

`udud` reads URLs on stdin and writes a structurally de-duplicated set on
stdout. It collapses URLs that are the *same endpoint* — same path shape,
same parameter set, templated IDs / UUIDs / hashes / slugs — down to one
real, first-seen representative, while keeping every distinct piece of
real surface a recon/pentest workflow actually needs.

Smallest output is **not** the goal. Losing zero real surface is.

## Features

- **Non-destructive** — keeps `.js`/`.html` sources, source-disclosure
  files (`.bak` `.sql` `.zip` `.phps`), open-redirect / SSRF / LFI
  parameters, and matrix-param auth endpoints (`;jsessionid=`) that
  aggressive dedupers silently delete.
- **Single-pass / streaming** — one pass, emit per line, no cross-line
  buffering. Memory stays **constant (~6.6 MB)** on a 119 MB / 640k-line
  stream; throughput is hundreds of MB/s.
- **Structural dedup** — folds per-item values (numeric IDs, UUIDs,
  hex digests, title slugs, `stem-<id>`) in the dedup *signature* only;
  the emitted line is always the real, untouched first-seen URL.
- **Clean by default** — no flags required. Scanner garbage, payload
  cache values, mangled hosts, and crawler spam are filtered out of the
  box; `-x` is a fully-raw escape hatch.
- **Fastest and lowest-RAM** of `uro` / `urldedupe` / `urless` / `uddup`
  while being the only one of them that is non-destructive. *([benchmark](#benchmark))*
- **Zero dependencies** — a single C file, no runtime, no config.

## Installation

```sh
git clone https://github.com/ayodyadsr/udud
cd udud
cc -O3 -march=native -flto -Wall -Wno-misleading-indentation -o udud udud.c
sudo install -m755 udud /usr/local/bin/udud
```

Optional benchmark helper (`fork`+`wait4`+`getrusage` wall/CPU/peak-RSS):

```sh
cc -O2 -o runstat runstat.c
```

## Usage

```
cat urls.txt | udud
```

Clean structural de-duplication is the default — no flags required.

```
usage: udud [-x][-a][-s][-k][-p][-W][-r][-V]

  -x   keep invalid URLs — fully raw, no cleaning (escape hatch)
  -a   keep all assets (don't filter .css/.png/.woff/…)
  -s   case-sensitive path matching
  -k   keep param values (dedup on full query, not just keys)
  -p   no path templating (don't fold numeric/UUID/hash/slug IDs)
  -W   opt out of wayback-noise handling
  -r   opt out of URL canonicalization
  -V   print  udud: <in> -> <out>  (peak RSS <n> KB)  to stderr
```

## Examples

```sh
# clean recon surface from an archive feed
gau example.com | udud > surface.txt

# combine multiple sources, dedupe once
cat gau.txt waybackurls.txt katana.txt | udud | tee urls.txt

# feed a param-fuzzing pipeline
gau example.com | udud | qsreplace FUZZ | anew params.txt

# show the reduction (stats on stderr, data still on stdout)
cat urls.txt | udud -V > deduped.txt

# pure structural dedup only, no cleaning
cat urls.txt | udud -x
```

## Benchmark

Full 5-tool head-to-head and line-by-line pentester audit:
**[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)**
· [ANALYSIS.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/results/ANALYSIS.md)

`apple.com` waybackurls — frozen 640,399-line / 119.6 MB snapshot:

| tool | output | wall | peak RSS | quality |
|---|---:|---:|---:|---|
| **udud** | **44 653** | **3.7 s** | **6.6 MB** | non-destructive — keeps 1790 `.html` + 28 `;jsessionid=` + 19 `.woa` + 624 `.js` |
| uro | 39 811 | 20.4 s | 28 MB | drops `.html`, **all** `;jsessionid=`, redirect endpoints |
| urldedupe | 133 412 | 5.2 s | 343 MB | barely dedupes (3× the lines), 52× the RAM |
| urless | 36 335 | 91.4 s | 39 MB | keyword blacklist deletes blog/news + `.html` + `;jsessionid=` |
| uddup | 0 | killed @300 s | — | O(n²) — no output even at a 30-min cap |

Fastest, lowest-RAM, **and** the only non-destructive tool. *best ≠ fewest lines.*

## How it works

For each line, in one pass: scheme repair → host/port normalization →
host sanity gates → split path/query/fragment → garbage gate → build a
dedup *signature* (path segments templated: digits→`N`, UUID→`U`,
hex≥12→`H`, slug→`S`, `stem-<id>`→`stem-#`; query reduced to its sorted
key set) → first-seen check in a hash set → emit the **original**
URL bytes verbatim.

The signature is what gets compared; the URL that gets printed is never
rewritten. No survivor buffer, so RAM is O(unique signatures), not
O(input) — that single-pass property is the whole design.

## License

[GNU Affero General Public License v3.0](LICENSE)

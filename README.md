<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Single-pass URL structural de-duplicator that keeps your attack surface.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue.svg"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-18.4MB%20%2F%20781k%20URLs-success.svg">
  <a href="https://github.com/ayodyadsr/udud-benchmark"><img src="https://img.shields.io/badge/benchmark-results-orange.svg"></a>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#examples">Examples</a> ·
  <a href="#benchmark">Benchmark</a> ·
  <a href="#how-it-works">How it works</a>
</p>

---

`udud` reads URLs from stdin and writes a deduplicated set to stdout. It
collapses URLs that hit the same endpoint (same path shape, same
parameter set, with IDs, UUIDs, hashes and slugs treated as templated)
down to the first one it saw. It does not delete the kinds of URLs a
pentester still wants to look at.

It does not try to produce the smallest output. It cuts noise without
dropping anything you would actually test.

## Features

- Non-destructive. Keeps `.js` and `.html` sources, source-disclosure
  files (`.bak`, `.sql`, `.zip`, `.phps`), open-redirect / SSRF / LFI
  parameters, and matrix-param auth endpoints like `;jsessionid=`.
  Aggressive dedupers drop these.
- Single-pass. One read of the stream, one line out at a time, no
  cross-line buffer. Memory is the set of unique signatures seen so far,
  not constant: it measures 18 MB on a 133 MB / 781k-line input, and
  stays in single digits on smaller inputs. It reads input at about
  15 MB/s.
- Structural. Numeric IDs, UUIDs, hex digests, title slugs and
  `stem-<id>` patterns are folded only inside the dedup signature. The
  line printed is the real first-seen URL, unmodified.
- Clean by default. No flags needed. Scanner junk, payload cache values,
  mangled hosts and crawler spam are dropped without asking. Pass `-x`
  if you want the raw stream with no cleaning.
- Single C file. No runtime, no config, nothing to install but the
  binary.

## Installation

```sh
git clone https://github.com/ayodyadsr/udud
cd udud
cc -O3 -march=native -flto -Wall -Wno-misleading-indentation -o udud udud.c
sudo install -m755 udud /usr/local/bin/udud
```

Optional benchmark helper (fork + wait4 + getrusage for wall, CPU and
peak RSS):

```sh
cc -O2 -o runstat runstat.c
```

## Usage

```sh
cat urls.txt | udud
```

That is the normal case. Clean structural dedup runs by default with no
flags.

```
usage: udud [-x] [-a] [-s] [-k] [-p] [-W] [-r] [-V]

  -x   keep invalid URLs, fully raw, no cleaning
  -a   keep all assets (do not filter .css/.png/.woff/...)
  -s   case-sensitive path matching
  -k   keep param values (dedup on the full query, not just keys)
  -p   no path templating (do not fold numeric/UUID/hash/slug IDs)
  -W   opt out of wayback-noise handling
  -r   opt out of URL canonicalization
  -V   print "udud: <in> -> <out> (peak RSS <n> KB)" to stderr
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

# structural dedup only, skip the cleaning
cat urls.txt | udud -x
```

## Benchmark

The full report, the statistics harness, every per-trial timing, and a
per-line audit of every removed URL are in
[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)
([BENCHMARK.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/BENCHMARK.md),
[AUDIT.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/AUDIT.md),
[ANONYMIZATION.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/ANONYMIZATION.md)).
The numbers below are quoted verbatim from that report. The cell at the
top is the largest corpus; the benchmark repo carries the prefix slices
(25k, 50k, 100k, 200k, 400k, full), the gau corpus, and the public
vulnweb corpus as well.

### Parameters

| field | value |
|---|---|
| corpus | `D_example_wb.full`, 781,398 lines, 134,533,990 bytes |
| sha256 | `9cd97dbcdd4c784075f0cc53a97bfa23833226357c553c2879ee02e6de2a63a2` |
| provenance | Wayback Machine capture of a commercial target, deterministically de-identified before release: monoalphabetic cipher with no fixed point over `[A-Za-z]`, brand-clean structural whitelists, three-check residue gate in `harness/verify_anon.py` |
| trials | N = 10 timed + 1 warmup per (tool, corpus) cell |
| statistic | mean wall with Student t 95% confidence interval; standard deviation, median and CoV also recorded in `raw/summary.csv` |
| clock | CPU governor = `performance` on all cores, `intel_pstate/no_turbo = 1`, `taskset -c 2`, ASLR off, page cache pre-warmed |
| hardware | x86_64, 8 cores, 16 GB RAM, Linux 6.12 (env manifest in `raw/environment.txt`) |
| compiler | `cc -O3 -march=native -flto -Wall -Wno-misleading-indentation` |
| versions | udud v14, uro 1.0.2, urldedupe 1.0.4, urless 2.7, uddup 0.9.3 |
| determinism | output sha256 stable across all 10 trials of every cell, recorded in `raw/trials.csv` |
| time-out | 300 s per trial, otherwise marked DNF |
| quality metric | endpoint-class retention, canonicalization-invariant (RFC 3986 syntax norm, dot-segment removal, DirectoryIndex, per-class templating), applied identically to truth and every tool |

### Results

| tool | output | wall (s, 95% CI) | peak RSS | quality |
|---|---:|---:|---:|---|
| **udud v14** | 125,837 | **9.364 +/- 0.296** | **18.4 MB** | js 99.25%, matrix folded with the auth endpoint kept, host 98.88%; per-line audit finds zero real surface lost |
| urldedupe 1.0.4 | 293,420 | 9.412 +/- 0.062 | 335.9 MB | near-verbatim passthrough, keeps every value variant |
| uro 1.0.2 | 78,470 | 39.763 +/- 0.184 | 35.1 MB | js 11.4%, matrix 0% |
| urless 2.7 | 74,737 | 172.161 +/- 1.024 | 45.3 MB | js 11.5%, matrix 0%, blog/news keyword blacklist |
| uddup 0.9.3 | DNF | > 300 s | n/a | O(n^2), no output beyond 50k lines |

udud is the fastest tool that finishes, uses the least RAM (about 18x
less than the only tool of comparable speed), and is the only one that
is lossless on real attack surface by a complete per-line audit.
Fewer output lines is not a better result here: urldedupe keeps the
most only because it barely deduplicates.

## How it works

Each line goes through one pass: repair the scheme, normalize host and
port, run host sanity gates, split path / query / fragment, drop
garbage, then build a dedup signature. In the signature, path segments
are templated (digits become `N`, UUID becomes `U`, hex of length 12 or
more becomes `H`, slug becomes `S`, `stem-<id>` becomes `stem-#`) and
the query is reduced to its sorted key set. The signature is looked up
in a hash set. If it is new, the original URL bytes are printed
unchanged.

Only the signature is compared. The URL that gets printed is never
rewritten. There is no survivor buffer, so memory scales with the
number of unique signatures, not with the size of the input. That is
the point of the single-pass design.

## License

[GNU Affero General Public License v3.0](LICENSE)
